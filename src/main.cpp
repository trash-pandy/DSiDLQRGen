#include <utility>
#include <vector>
#include <map>
#include <string>
#include <functional>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <ranges>
#include <cmath>

#if defined(_WINDOWS)
#define NOGDI
#define NOUSER
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#else
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

#include <raylib.h>
#include <mongoose.h>

#include "qrgen.h"

using namespace std::filesystem;

class UI;

std::vector<std::unique_ptr<UI>> pages;
std::vector<std::unique_ptr<UI>> removed_pages;
bool should_close = false;

template <class T>
	requires std::is_base_of_v<UI, T>
void PushPage()
{
	pages.push_back(std::make_unique<T>());
}

void PopPage()
{
	removed_pages.emplace_back(std::move(pages.back()));
	pages.pop_back();
}

bool IsKeyPressedRepeat(int key)
{
	static std::map<int, double> start_time;
	static std::map<int, double> delay;
	constexpr double initial_delay = 0.4;
	constexpr double after_delay = 0.1;

	if (IsKeyPressed(key))
	{
		start_time[key] = GetTime();
		delay[key] = initial_delay;
		return true;
	}
	if (IsKeyReleased(key))
	{
		start_time[key] = std::numeric_limits<double>::max();
	}
	if (GetTime() - start_time[key] > delay[key] && IsKeyDown(key))
	{
		start_time[key] = GetTime();
		delay[key] = after_delay;
		return true;
	}

	return false;
}

bool DoKeyboardNav(int& selected, const int max, int key_neg = KEY_UP, int key_pos = KEY_DOWN)
{
	if (selected > 0 && IsKeyPressedRepeat(key_neg))
	{
		selected--;
	}

	if (selected < max - 1 && IsKeyPressedRepeat(key_pos))
	{
		selected++;
	}

	if (IsKeyPressed(KEY_ENTER))
	{
		return true;
	}

	return false;
}

struct TextBox
{
	std::string text;
	int pos = 0;
	int text_size = 20;
	bool edit_mode = false;
	Rectangle rect;
	Color background = WHITE;
	Color border_color = LIGHTGRAY;
	Color text_color = BLACK;
};

void DoTextBox(TextBox& textbox)
{
	DrawRectangleLinesEx(textbox.rect, 2, textbox.border_color);
	BeginScissorMode(textbox.rect.x + 2, textbox.rect.y, textbox.rect.width - 4, textbox.rect.height);
	if (textbox.edit_mode)
	{
		DoKeyboardNav(textbox.pos, textbox.text.size() + 1, {KEY_LEFT}, {KEY_RIGHT});
		if (IsKeyPressedRepeat(KEY_BACKSPACE) && textbox.pos > 0)
		{
			auto ctrl_pressed = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
			if (ctrl_pressed)
			{
				textbox.text.erase(0, textbox.pos);
				textbox.pos = 0;
			}
			else
			{
				textbox.text.erase(--textbox.pos, 1);
			}
		}
		auto char_pressed = GetCharPressed();
		if (char_pressed >= 32 && char_pressed <= 125)
		{
			textbox.text.insert(textbox.pos++, (char*)&char_pressed, 1);
		}

		if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_V))
		{
			const std::string clipboard = GetClipboardText();
			textbox.text.insert(textbox.pos, clipboard);
			textbox.pos += clipboard.size();
		}
	}
	auto partial = textbox.text.substr(0, textbox.pos);
	auto cursor_x = MeasureText(partial.c_str(), textbox.text_size) + 1;
	if (textbox.pos == 0) cursor_x += 0;
	DrawText(textbox.text.c_str(), textbox.rect.x + 4, textbox.rect.y + textbox.rect.height / 2 - textbox.text_size / 2,
		textbox.text_size, textbox.text_color);
	if (textbox.edit_mode)
	{
		DrawLine(textbox.rect.x + 4 + cursor_x, textbox.rect.y + 2, textbox.rect.x + 4 + cursor_x,
			textbox.rect.y + textbox.rect.height - 2, textbox.text_color);
	}
	EndScissorMode();
}

struct Button
{
	std::string text = "";
	int text_size = 20;
	Rectangle rect;
	Color background = WHITE;
	Color border_color = LIGHTGRAY;
	Color text_color = BLACK;
};

void DoButton(Button& button)
{
	DrawRectangleLinesEx(button.rect, 2, button.border_color);
	auto text_width = MeasureText(button.text.c_str(), button.text_size);
	BeginScissorMode(button.rect.x + 2, button.rect.y, button.rect.width - 4, button.rect.height);
	DrawText(button.text.c_str(), button.rect.x + button.rect.width / 2 - text_width / 2,
		button.rect.y + button.rect.height / 2 - button.text_size / 2,
		button.text_size, button.text_color);
	EndScissorMode();
}

class UI
{
public:
	KeyboardKey leave_key = KEY_ESCAPE;

	virtual ~UI() = default;

	UI() = default;
	UI(const UI& other) = default;
	UI(UI&& other) = default;
	UI& operator=(const UI& other) = default;
	UI& operator=(UI&& other) = default;

	virtual void draw()
	{
	}
};

class QRHostPage final : public UI
{
	Texture texture {};
	mg_mgr mgr {};

public:
	static std::vector<std::pair<std::string, std::string>> files;

	static void http_serve(mg_connection* c, int ev, void* ev_data, void* fn_data)
	{
		if (ev == MG_EV_HTTP_MSG)
		{
			const auto hm = static_cast<mg_http_message*>(ev_data);
			constexpr mg_http_serve_opts opts = {};
			for (const auto& [loc, file] : files)
			{
				auto path = "/" + loc;
				if (mg_http_match_uri(hm, path.c_str()))
				{
					mg_http_serve_file(c, hm, file.c_str(), &opts);
				}
			}
		}
	}

	QRHostPage()
		: UI()
	{
		std::string local_ip = "0.0.0.0";
#if defined(_WINDOWS)
		ULONG buf_len = 0;
		GetAdaptersInfo(nullptr, &buf_len);
		const auto ip_adapter_info = new IP_ADAPTER_INFO[buf_len / sizeof(IP_ADAPTER_INFO)];
		GetAdaptersInfo(ip_adapter_info, &buf_len);

		for (auto adapter_iter = ip_adapter_info; adapter_iter != nullptr; adapter_iter = adapter_iter->Next)
		{
			if (adapter_iter->GatewayList.IpAddress.String[0] != '0')
			{
				local_ip = adapter_iter->IpAddressList.IpAddress.String;
			}
		}
		delete[] ip_adapter_info;
#else
		ifaddrs* if_list = nullptr;
		getifaddrs(&if_list);
		for (const ifaddrs* entry = if_list; entry != nullptr; entry = entry->ifa_next)
		{
			const auto if_addr = entry->ifa_addr;
			if (if_addr != nullptr && if_addr->sa_family == AF_INET)
			{
				const auto socket_addr = &((struct sockaddr_in*) if_addr)->sin_addr;
				char human_addr[INET_ADDRSTRLEN];
				inet_ntop(AF_INET, socket_addr, human_addr, INET_ADDRSTRLEN);
				if (std::string(human_addr, 3) == "127") continue;
				local_ip = human_addr;
				break;
			}
		}
		freeifaddrs(if_list);

		std::cout << local_ip << std::endl;
#endif

		const std::string base = "http://" + local_ip + ":37435";
		std::vector<std::pair<std::string, std::string>> remote_files;
		remote_files.reserve(files.size());
		for (auto loc : files | std::views::keys)
		{
			remote_files.emplace_back(loc, base + "/" + loc);
		}

		auto qr = MakeQr(remote_files);
		texture = MakeTextureFromQr(qr);
		QRcode_free(qr);

		mg_mgr_init(&mgr);
		mg_http_listen(&mgr, base.c_str(), http_serve, &mgr);

		std::cout << "Hosting server on " << base << std::endl;
	}

	~QRHostPage() override
	{
		mg_mgr_free(&mgr);
		UnloadTexture(texture);
	}

	void draw() override
	{
		int width = GetScreenWidth();
		int height = GetScreenHeight();

		mg_mgr_poll(&mgr, 33);

		BeginDrawing();
		if (texture.width != 0)
		{
			ClearBackground(WHITE);
			int scale;
			if (width >= height)
			{
				scale = std::floor((width - 40) / texture.width);
			}
			else
			{
				scale = std::floor((height - 40) / texture.height);
			}
			DrawTextureEx(texture, Vector2 {20, 20}, 0, scale, WHITE);
		}
		EndDrawing();
	}
};

std::vector<std::pair<std::string, std::string>> QRHostPage::files;

class AddFilePage final : public UI
{
public:
	static path file;

private:
	int selected = 0;
	TextBox name_box = TextBox {
		.text = file.filename().generic_string(),
		.pos = static_cast<int>(file.filename().generic_string().size())
	};
	Button enter_button = Button {
		.text = "Add"
	};

public:
	void draw() override
	{
		const std::vector<std::function<void()>> menu = {
			[&]
			{
				name_box.pos = name_box.text.size();
				name_box.edit_mode = true;
				leave_key = KEY_NULL;
			},
			[&]
			{
				QRHostPage::files.emplace_back(name_box.text, file.generic_string());
				PopPage();
				PopPage();
			}
		};

		if (DoKeyboardNav(selected, menu.size()))
		{
			menu[selected]();
		}

		if (IsKeyPressed(KEY_ESCAPE))
		{
			name_box.edit_mode = false;
			leave_key = KEY_ESCAPE;
		}

		BeginDrawing();
		{
			ClearBackground(WHITE);

			auto sd_length = MeasureText("sd:/", 20) + 2;
			DrawText("sd:/", 20, 22, 20, BLACK);

			name_box.rect = Rectangle {
				static_cast<float>(20 + sd_length), 20, static_cast<float>(GetScreenWidth() - 40 - sd_length), 24
			};
			name_box.border_color = selected == 0 ? DARKGRAY : LIGHTGRAY;
			DoTextBox(name_box);

			enter_button.rect = Rectangle {
				static_cast<float>(GetScreenWidth() - 20 - 80), 20 + 24 + 2, 80, 22
			};
			enter_button.border_color = selected == 1 ? DARKGRAY : LIGHTGRAY;
			DoButton(enter_button);
		}
		EndDrawing();
	}
};

path AddFilePage::file;

class AddUrlPage final : public UI
{
	int selected = 0;
	TextBox name_box = TextBox {};
	TextBox url_box = TextBox {};
	Button enter_button = Button {
		.text = "Add"
	};

public:
	void draw() override
	{
		const std::vector<std::function<void()>> menu = {
			[&]
			{
				name_box.pos = name_box.text.size();
				name_box.edit_mode = true;
				leave_key = KEY_NULL;
			},
			[&]
			{
				url_box.pos = url_box.text.size();
				url_box.edit_mode = true;
				leave_key = KEY_NULL;
			},
			[&]
			{
				QRHostPage::files.emplace_back(name_box.text, url_box.text);
				PopPage();
			}
		};

		auto last_selected = selected;
		if (DoKeyboardNav(selected, menu.size()))
		{
			menu[selected]();
		}

		if (last_selected != selected || IsKeyPressed(KEY_ESCAPE))
		{
			name_box.edit_mode = false;
			url_box.edit_mode = false;
			leave_key = KEY_ESCAPE;
		}

		BeginDrawing();
		{
			ClearBackground(WHITE);

			auto name_label = MeasureText("sd:/", 20) + 2;
			DrawText("sd:/", 20, 22, 20, BLACK);

			name_box.rect = Rectangle {
				static_cast<float>(20 + name_label), 20, static_cast<float>(GetScreenWidth() - 40 - name_label), 24
			};
			name_box.border_color = selected == 0 ? DARKGRAY : LIGHTGRAY;
			DoTextBox(name_box);

			auto url_label = MeasureText("url: ", 20) + 2;
			DrawText("url: ", 20, 48, 20, BLACK);

			url_box.rect = Rectangle {
				static_cast<float>(20 + url_label), 46, static_cast<float>(GetScreenWidth() - 40 - url_label), 24
			};
			url_box.border_color = selected == 1 ? DARKGRAY : LIGHTGRAY;
			DoTextBox(url_box);

			enter_button.rect = Rectangle {
				static_cast<float>(GetScreenWidth() - 20 - 80), 20 + 24 + 2 + 24 + 2, 80, 22
			};
			enter_button.border_color = selected == 2 ? DARKGRAY : LIGHTGRAY;
			DoButton(enter_button);
		}
		EndDrawing();
	}
};

class FileViewerPage final : public UI
{
	struct FileMeta
	{
		std::string name;
		path file_path;
		bool is_dir;
	};

	int selected = 0;
	bool dirty = true;
	std::vector<FileMeta> cached_files;
	path file_path = current_path();

public:
	void draw() override
	{
		if (dirty)
		{
			dirty = false;
			selected = 0;
			cached_files.clear();
			cached_files.push_back(FileMeta {
				.name = "..",
				.file_path = file_path.parent_path(),
				.is_dir = true
			});
			cached_files.push_back(FileMeta {
				.name = ".",
				.file_path = current_path(),
				.is_dir = true
			});
			for (const auto& dir : directory_iterator(file_path))
			{
				cached_files.push_back(FileMeta {
					.name = dir.path().filename().generic_string(),
					.file_path = dir.path(),
					.is_dir = is_directory(dir.path())
				});
			}
		}

		if (DoKeyboardNav(selected, cached_files.size()))
		{
			auto selected_path = cached_files[selected].file_path;
			if (is_directory(selected_path))
			{
				dirty = true;
				file_path = selected_path;
			}
			else
			{
				AddFilePage::file = selected_path;
				PushPage<AddFilePage>();
			}
		}

		BeginDrawing();
		{
			ClearBackground(WHITE);

			auto path_str = file_path.generic_string();
			auto cur_path_width = MeasureText(path_str.c_str(), 20);
			auto dotdotdot = MeasureText("...", 20) + 2;
			if (GetScreenWidth() - 40 - cur_path_width >= 0)
			{
				dotdotdot = 0;
			}
			BeginScissorMode(20, 20, GetScreenWidth() - 40, GetScreenHeight() - 40);
			{
				DrawText(path_str.c_str(), GetScreenWidth() - 20 - cur_path_width, 20, 20, BLACK);
			}
			EndScissorMode();

			if (dotdotdot != 0)
			{
				//DrawText("...", 20, 20, 20, BLACK);
				DrawRectangleGradientH(20, 20, 80, 20, WHITE, ColorAlpha(WHITE, 0));
			}
			DrawLine(20, 41, GetScreenWidth() - 20, 41, BLACK);

			BeginScissorMode(20, 44, GetScreenWidth() - 20, GetScreenHeight() - 40);
			{
				int y = 44 - 22 * selected;
				for (int i = 0; i < cached_files.size(); i++)
				{
					auto [name, _, is_dir] = cached_files[i];
					auto left = 20;
					if (i == selected)
					{
						left += MeasureText("> ", 20);
						DrawText("> ", 20, y, 20, BLACK);
					}
					DrawText(name.c_str(), left, y, 20, is_dir ? BLUE : BLACK);
					y += 22;
				}
			}
			EndScissorMode();
		}
		EndDrawing();
	}
};

class FrontPage final : public UI
{
	int selected = 0;

public:
	void draw() override
	{
		static double copied_json_at = ~0;
		static double cleared_file_list_at = ~0;
		std::vector<std::pair<const char*, std::function<void()>>> menu = {
			std::make_pair("Add file", []
			{
				PushPage<FileViewerPage>();
			}),
			std::make_pair("Add url", []
			{
				PushPage<AddUrlPage>();
			}),
			std::make_pair("Copy json", [&]
			{
				std::vector<std::pair<std::string, std::string>> remote_files;
				for (auto [loc, file] : QRHostPage::files)
				{
					remote_files.emplace_back(loc, "http://PREVIEW_IP/" + loc);
				}
				const auto text = MakeQrJson(remote_files);
				SetClipboardText(text.c_str());
				copied_json_at = GetTime();
			}),
			std::make_pair("Make QR", []
			{
				PushPage<QRHostPage>();
			}),
			std::make_pair("Clear file list", [&]
			{
				QRHostPage::files.clear();
				cleared_file_list_at = GetTime();
			}),
			std::make_pair("Exit", []
			{
				should_close = true;
			}),
		};

		if (DoKeyboardNav(selected, menu.size()))
		{
			menu[selected].second();
		}

		BeginDrawing();
		{
			ClearBackground(WHITE);

			for (int i = 0; i < menu.size(); i++)
			{
				auto [menu_text, fn] = menu[i];
				auto left = 20;
				if (i == selected)
				{
					left += MeasureText("> ", 20);
					DrawText("> ", 20, 20 + i * 24, 20, BLACK);
				}
				auto color = BLACK;
				if (i == 2 && GetTime() - copied_json_at < 1)
				{
					color = GREEN;
				}
				if (i == 4 && GetTime() - cleared_file_list_at < 1)
				{
					color = GREEN;
				}
				DrawText(menu_text, left, 20 + i * 24, 20, color);
			}
		}
		EndDrawing();
	}
};

int main(int argc, char** argv)
{
	SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
	InitWindow(400, 400, "DSiDLQRGen");
	SetExitKey(0);

	PushPage<FrontPage>();

	while (!should_close)
	{
		should_close = WindowShouldClose();

		removed_pages.clear();
		if (IsKeyPressed(pages.back()->leave_key) && pages.size() > 1)
		{
			pages.pop_back();
		}

		pages.back()->draw();
	}
}
