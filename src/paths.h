// Copyright 2020 Allan Riordan Boll

#ifndef SRC_PATHS_H_
#define SRC_PATHS_H_

#include <string>
#include <vector>

using std::string;
using std::vector;

extern const vector<string> image_extensions;
extern const vector<string> video_extensions;

string normalize_path(string path);
string basename(string path);
bool is_image(string path);
bool is_video(string path);

#endif  // SRC_PATHS_H_
