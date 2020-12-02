// Copyright 2020 Allan Riordan Boll

#ifndef SRC_STRING_H_
#define SRC_STRING_H_

#include <string>

using std::string;
using std::wstring;

string sha256(const string str);

string encodeBase64(const unsigned char *input, int n);

string PrettifySentence(string s);

#ifdef __WXMSW__

wstring localPathUnicode(string local_path);

#else

string localPathUnicode(string local_path);

#endif

#endif  // SRC_STRING_H_
