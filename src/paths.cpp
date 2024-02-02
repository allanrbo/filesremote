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

const vector<string> image_extensions = {".jpg", ".jpeg", ".png", ".gif", ".bmp", ".svg"};
const vector<string> video_extensions = {".mp4", ".mkv", ".avi", ".mov", ".webm"};

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


string string_to_lower(string str) {
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str;
}

bool is_path_extension_in_vector(string path, const vector<string> extension_list) {
    string extension = string_to_lower(fs::path(path).extension());
    auto it = std::find(extension_list.begin(), extension_list.end(), extension);
    return it != std::end(extension_list);
}

bool is_image(string path) {
    return is_path_extension_in_vector(path, image_extensions);
}

bool is_video(string path) {
    return is_path_extension_in_vector(path, video_extensions);
}
