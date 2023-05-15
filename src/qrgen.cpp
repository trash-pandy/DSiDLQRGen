#include "qrgen.h"

#include <sstream>

std::string MakeQrJson(std::vector<std::pair<std::string, std::string>> dls)
{
	std::ostringstream out;
	out << "{";
	for (auto i = dls.begin(); i != dls.end(); ++i)
	{
		auto [loc, url] = *i;
		out << "\"sd:/" << loc << "\":\"" << url << '"';
		if (std::next(i) != dls.end())
		{
			out << ',';
		}
	}
	out << "}";

	return out.str();
}

QRcode* MakeQr(std::vector<std::pair<std::string, std::string>> dls)
{
	const auto json = MakeQrJson(std::move(dls));
	return QRcode_encodeString(json.c_str(), 0, QR_ECLEVEL_M, QR_MODE_8, 1);
}

QRcode* MakeQr(const std::string& data)
{
	return QRcode_encodeString(data.c_str(), 0, QR_ECLEVEL_M, QR_MODE_8, 1);
}

Texture2D MakeTextureFromQr(const QRcode* qr)
{
	Image image;
	image.format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE;
	image.width = image.height = qr->width;
	image.mipmaps = 1;
	const auto image_data = new unsigned char[qr->width * qr->width];
	for (int i = 0; i < qr->width * qr->width; i++)
	{
		image_data[i] = 255u - (qr->data[i] & 1u) * 255u;
	}
	image.data = (void*) image_data;
	const auto texture = LoadTextureFromImage(image);
	delete[] image_data;

	return texture;
}
