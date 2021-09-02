// Copyright 2021 Allan Riordan Boll

#include "src/storageunits.h"

#include <iomanip>
#include <sstream>
#include <string>

using std::stringstream;
using std::to_string;

static string Rounded(double d, int precision) {
    stringstream ss;
    ss << std::fixed << std::setprecision(precision) << d;
    return ss.str();
}

string size_string(uint64_t bytes) {
    if (bytes < 1024) {
        return to_string(bytes) + " bytes";
    }

    double size = bytes;
    size = size / 1024.0;
    if (size < 1024) {
        return Rounded(size, 1) + " KiB";
    }

    size = size / 1024.0;
    if (size < 1024) {
        return Rounded(size, 2) + " MiB";
    }

    size = size / 1024.0;
    if (size < 1024) {
        return Rounded(size, 2) + " GiB";
    }

    size = size / 1024.0;
    return Rounded(size, 2) + " TiB";
}
