// Copyright 2020 Allan Riordan Boll

#ifndef SRC_HOSTDESC_H_
#define SRC_HOSTDESC_H_

#include <string>
#include <vector>

using std::string;
using std::vector;

class HostDesc {
public:
    string entered_;
    string host_;  // This is the DNS name or IP.
    string display_host_;  // This is what will be in the "Host"-line of ~/.ssh/config.
    string username_;
    int port_ = 22;
    vector<string> identity_files_;

    HostDesc() {}

    explicit HostDesc(string host, string identity_file);

    string ToString();

    string ToStringNoCol();

    string ToStringNoUser();

    string ToStringNoUserNoCol();
};

#endif  // SRC_HOSTDESC_H_
