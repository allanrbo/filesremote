// Copyright 2020 Allan Riordan Boll

#ifndef SRC_HOSTDESC_H_
#define SRC_HOSTDESC_H_

#include <string>

using std::string;

class HostDesc {
public:
    string entered_;
    string host_;
    string username_;
    int port_ = 22;

    HostDesc() {}

    explicit HostDesc(string host);

    string ToString();

    string ToStringNoCol();

    string ToStringNoUser();

    string ToStringNoUserNoCol();
};

#endif  // SRC_HOSTDESC_H_
