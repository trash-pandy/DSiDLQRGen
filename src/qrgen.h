#pragma once

#include <string>
#include <vector>

#include <qrencode.h>
#include <raylib.h>

std::string MakeQrJson(std::vector<std::pair<std::string, std::string>> dls);

QRcode* MakeQr(std::vector<std::pair<std::string, std::string>> dls);
QRcode* MakeQr(const std::string& data);

Texture2D MakeTextureFromQr(const QRcode* code);
