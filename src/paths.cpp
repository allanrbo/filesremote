// Copyright 2020 Allan Riordan Boll

#include "src/paths.h"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

using std::replace;
using std::string;
using std::stringstream;
using std::vector;

namespace fs = std::filesystem;

string normalize_path(string path) {
    replace(path.begin(), path.end(), '\\', '/');

    stringstream s(path);
    string segment;
    vector<string> parts;
    while (getline(s, segment, '/')) {
        if (segment.empty() || segment == ".") {
            continue;
        } else if (segment == "..") {
            if (!parts.empty()) {
                parts.pop_back();
            } else {
                continue;
            }
        } else {
            parts.push_back(segment);
        }
    }

    string r = "";
    for (int i = 0; i < parts.size(); ++i) {
        if (i == 0 && parts[0].length() == 2 && parts[0][1] == ':') {
            // Windows drive letter part.
        } else {
            r += "/";
        }
        r += parts[i];
    }

    if (r.empty()) {
        return "/";
    }

    return r;
}

string basename(string path) {
    replace(path.begin(), path.end(), '\\', '/');

    stringstream s(path);
    string segment;
    vector<string> parts;
    while (getline(s, segment, '/')) {}
    return segment;
}

bool is_image(string path) {
    string extension = fs::path(path).extension();
    auto it = std::find(image_extensions.begin(), image_extensions.end(), extension);
    return it != std::end(image_extensions);
}

bool is_video(string path) {
    string extension = fs::path(path).extension();
    auto it = std::find(video_extensions.begin(), video_extensions.end(), extension);
    return it != std::end(video_extensions);
}
