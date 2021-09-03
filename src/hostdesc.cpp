// Copyright 2020 Allan Riordan Boll

#include "src/hostdesc.h"

#include <wx/wx.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <regex>  // NOLINT
#include <sstream>
#include <string>

#ifndef __WXOSX__

#include <filesystem>

#endif

using std::ifstream;
using std::invalid_argument;
using std::istringstream;
using std::regex;
using std::string;
using std::to_string;

#ifndef __WXOSX__
using std::filesystem::exists;
#else
#include "src/filesystem.osx.polyfills.h"
#endif

static string strip_quotes(string s) {
    if (s[0] == '"') {
        s = s.substr(1, s.length() - 2);
    }

    return s;
}

HostDesc::HostDesc(string host, string identity_file) {
    this->entered_ = host;
    this->username_ = wxGetUserId();
    this->host_ = host;
    this->port_ = 22;

#ifdef __WXMSW__
    // Windows usually title-cases usernames, but the hosts we will likely be SSH'ing to are usually lower cased.
    transform(this->username_.begin(), this->username_.end(), this->username_.begin(), ::tolower);
#endif

    // Check if there's a username given.
    bool username_given = false;
    if (this->host_.find("@") != string::npos) {
        username_given = true;
        int i = this->host_.find("@");
        this->username_ = this->host_.substr(0, i);
        this->host_ = this->host_.substr(i + 1);
    }

    // Check if there's a port number given.
    bool port_given = false;
    if (this->host_.find(":") != string::npos) {
        port_given = true;

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

    // The "Host"-lines in ~/.ssh/config may differ from the actual DNS name or IP in the "HostName" field.
    // The display_host_ field represents what will be in the "Host"-line of ~/.ssh/config.
    this->display_host_ = this->host_;

    // Prepare where to look for ssh config files.
    vector<string> try_ssh_config_paths;
#ifdef __WXMSW__
    string home = getenv("HOMEPATH");
    try_ssh_config_paths.push_back("C:\\Program Files\\Git\\etc\\ssh\\ssh_config");
#else
    string home = getenv("HOME");
#endif
    try_ssh_config_paths.push_back(home + "/.ssh/config");

    // Look through each potential ssh config file.
    for (auto path : try_ssh_config_paths) {
        try {
            if (!exists(path)) {
                continue;
            }

            // Parse the .ssh/config file.
            ifstream infile(path);
            string line, cur_host;
            while (getline(infile, line)) {
                istringstream iss(line);
                string cmd;
                iss >> cmd;
                transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
                if (cmd == "host") {
                    iss >> cur_host;
                    cur_host = strip_quotes(cur_host);
                    transform(cur_host.begin(), cur_host.end(), cur_host.begin(), ::tolower);
                } else if (cmd == "hostname" && (cur_host == this->display_host_ || cur_host.empty())) {
                    string hostname;
                    iss >> hostname;
                    hostname = strip_quotes(hostname);
                    this->host_ = hostname;
                } else if (cmd == "identityfile" && (cur_host == this->display_host_ || cur_host.empty())) {
                    string identity_file;
                    iss >> identity_file;
                    identity_file = strip_quotes(identity_file);

                    // Expand homedir tilde.
                    if (identity_file[0] == '~') {
                        identity_file = home + identity_file.substr(1);
                    }

                    this->identity_files_.push_back(identity_file);
                } else if (cmd == "user" && !username_given && (cur_host == this->display_host_ || cur_host.empty())) {
                    string user;
                    iss >> user;
                    user = strip_quotes(user);
                    this->username_ = user;
                } else if (cmd == "port" && !port_given && (cur_host == this->display_host_ || cur_host.empty())) {
                    string ps;
                    iss >> ps;
                    ps = strip_quotes(ps);

                    if (!all_of(ps.begin(), ps.end(), ::isdigit)) {
                        throw invalid_argument("non-digit port number in ssh config");
                    }
                    this->port_ = stoi(string(ps));
                    if (!(0 < this->port_ && this->port_ < 65536)) {
                        throw invalid_argument("invalid port number ssh config");
                    }
                }
            }
        } catch (invalid_argument) {
            throw;
        } catch (...) {
            // Probably permission error. Continue to try the next config path.
            continue;
        }
    }

    // Additional standard paths to load the key from.
    this->identity_files_.push_back(home + "/.ssh/id_rsa_" + this->host_);  // Observed openssh client use this.
    this->identity_files_.push_back(home + "/.ssh/id_dsa_" + this->host_);
    this->identity_files_.push_back(home + "/.ssh/id_rsa");
    this->identity_files_.push_back(home + "/.ssh/id_dsa");

    // If an identify file was explicitly given as param, then use that instead.
    if (!identity_file.empty()) {
        this->identity_files_.clear();
        this->identity_files_.push_back(identity_file);
    }

    // An allow-list regex would be better, but too tricky due to internationalized domain names.
    if (regex_search(this->host_, regex("[/\\\\]"))) {
        throw invalid_argument("invalid host name");
    }
}

string HostDesc::ToString() {
    string s = this->username_ + "@" + this->host_ + ":" + to_string(this->port_);
    if (this->host_ != this->display_host_) {
        s += " (" + this->display_host_ + ")";
    }
    return s;
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
