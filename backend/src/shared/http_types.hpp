#pragma once

#include <map>
#include <string>
#include <vector>

struct HttpRequest {
  std::string method;
  std::string target;
  std::map<std::string, std::string> headers;
  std::vector<char> body;
};
