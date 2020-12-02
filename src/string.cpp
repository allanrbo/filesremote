// Copyright 2020 Allan Riordan Boll

#include "src/string.h"

#include <wx/wx.h>

#include <openssl/evp.h>
#include <openssl/sha.h>

#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

using std::make_unique;
using std::runtime_error;
using std::string;
using std::stringstream;
using std::wstring;

// Based on https://stackoverflow.com/a/10632725/40645
string sha256(const string str) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, str.c_str(), str.size());
    SHA256_Final(hash, &sha256);
    stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

// Based on https://stackoverflow.com/a/63711162/40645
string encodeBase64(const unsigned char *input, int n) {
    const auto predicted_len = 4 * ((n + 2) / 3);  // predict output size
    const auto output_buffer{make_unique<char[]>(predicted_len + 1)};
    const auto output_len = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(output_buffer.get()), input, n);
    if (predicted_len != static_cast<uint64_t>(output_len)) {
        throw runtime_error("base64 encoding error");
    }
    return output_buffer.get();
}

// Make strings, such as error strings, a little easier on the eyes.
string PrettifySentence(string s) {
    if (s.size() > 0) {
        s[0] = toupper(s[0]);
    }
    if (s.size() > 0 && s[s.size() - 1] != '.') {
        s += ".";
    }
    return s;
}

#ifdef __WXMSW__

wstring localPathUnicode(string local_path) {
    return wxString::FromUTF8(local_path).ToStdWstring();
}

#else

string localPathUnicode(string local_path) {
    return local_path;
}

#endif
