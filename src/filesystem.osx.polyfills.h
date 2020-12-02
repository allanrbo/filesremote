// Copyright 2020 Allan Riordan Boll

#ifndef SRC_FILESYSTEM_OSX_POLYFILLS_H_
#define SRC_FILESYSTEM_OSX_POLYFILLS_H_

#ifdef __WXOSX__
// Polyfills for these funcs that got introduced only in MacOS 10.15.

#include <sys/stat.h>

#include <sstream>
#include <string>

using std::stringstream;

typedef uint64_t file_time_type;

static bool exists(string path) {
    struct stat sb;
    return stat(path.c_str(), &sb) == 0;
}

static void create_directories(string path) {
    string cur = "";
    stringstream s(path);
    string segment;
    while (getline(s, segment, '/')) {
        if (segment.empty()) {
            continue;
        }
        cur += "/" + segment;
        if (!exists(cur)) {
            mkdir(cur.c_str(), 0700);
        }
    }
}

static file_time_type last_write_time(string path) {
    struct stat attr;
    stat(path.c_str(), &attr);
    return attr.st_mtime;
}

static void remove(string path) {
    remove(path.c_str());
}

static void remove_all(string path) {
    wxExecute(wxString::FromUTF8("rm -fr \"" + path + "\""), wxEXEC_SYNC);
}
#endif

#endif  // SRC_FILESYSTEM_OSX_POLYFILLS_H_
