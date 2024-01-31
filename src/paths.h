// Copyright 2020 Allan Riordan Boll

#ifndef SRC_PATHS_H_
#define SRC_PATHS_H_

#include <string>

using std::string;
using std::vector;

extern const vector<string> image_extensions = {".jpg", ".jpeg", ".png", ".gif"};
extern const vector<string> video_extensions = {".mp4", ".mkv", ".avi"};

string normalize_path(string path);

string basename(string path);

#endif  // SRC_PATHS_H_
