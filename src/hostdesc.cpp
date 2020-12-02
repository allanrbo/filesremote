// Copyright 2020 Allan Riordan Boll

#include "src/hostdesc.h"

#include <wx/wx.h>

#include <algorithm>
#include <regex>  // NOLINT
#include <string>

using std::invalid_argument;
using std::regex;
using std::string;
using std::to_string;

HostDesc::HostDesc(string host) {
    this->entered_ = host;
    this->username_ = wxGetUserId();
    this->host_ = host;
    this->port_ = 22;

#ifdef __WXMSW__
    // Windows usually title-cases usernames, but the hosts we will likely be SSH'ing to are usually lower cased.
    transform(this->username_.begin(), this->username_.end(), this->username_.begin(), ::tolower);
#endif

    if (this->host_.find("@") != string::npos) {
        int i = this->host_.find("@");
        this->username_ = this->host_.substr(0, i);
        this->host_ = this->host_.substr(i + 1);
    }

    if (this->host_.find(":") != string::npos) {
        int i = this->host_.find(":");

        string ps = string(this->host_.substr(i + 1));
        if (!all_of(ps.begin(), ps.end(), ::isdigit)) {
            throw invalid_argument("non-digit port number");
        }
        this->port_ = stoi(string(ps));
        if (!(0 < this->port_ && this->port_ < 65536)) {
            throw invalid_argument("invalid port number");
        }

        this->host_ = this->host_.substr(0, i);
    }

    // An allow-list regex would be better, but too tricky due to internationalized domain names.
    if (regex_search(this->host_, regex("[/\\\\]"))) {
        throw invalid_argument("invalid host name");
    }
}

string HostDesc::ToString() {
    return this->username_ + "@" + this->host_ + ":" + to_string(this->port_);
}

string HostDesc::ToStringNoCol() {
    return this->username_ + "@" + this->host_ + "_" + to_string(this->port_);
}

string HostDesc::ToStringNoUser() {
    return this->host_ + ":" + to_string(this->port_);
}

string HostDesc::ToStringNoUserNoCol() {
    return this->host_ + "_" + to_string(this->port_);
}
