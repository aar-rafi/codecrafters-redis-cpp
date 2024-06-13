#include <iostream>
#include <vector>
using namespace std;

enum RESPType
{
  SIMPLE_STRING,
  BULK_STRING,
  ARRAY,
  ERROR,
  INTEGER
};

struct RESP
{
  int count; // number of elements in the array
  int type;
  string sub_msg;
  vector<string> msgs;
};

RESP parseResp(string buffer)
{
  RESP resp;
  resp.count = 0;
  resp.sub_msg = "";
  resp.msgs.clear();
  switch (buffer[0])
  {
  case '+':
  {
    resp.type = SIMPLE_STRING;
    resp.sub_msg = buffer.substr(1, buffer.length() - 3);
  }
  break;

  case '$':
  {
    resp.type = BULK_STRING;
    int i = buffer.find('\r');
    int len = stoi(buffer.substr(1, i - 1));
    resp.sub_msg = buffer.substr(i + 2, len);
  }
  break;

  case '*':
  {
    resp.type = ARRAY;
    int i = buffer.find('\r');
    resp.count = stoi(buffer.substr(1, i - 1));
    i += 2;
    int j = 0;
    for (int k = 0; k < resp.count; k++)
    {
      j = buffer.find('\r', i);
      int len = stoi(buffer.substr(i + 1, j - i - 1)); // i->size
      resp.msgs.push_back(buffer.substr(j + 2, len));
      i = j + len + 4;
    }
  }
  break;

  case '-':
  {
    resp.type = ERROR;
    resp.sub_msg = buffer.substr(1, buffer.length() - 3);
  }
  break;

  case ':':
  {
    resp.type = INTEGER;
    resp.sub_msg = buffer.substr(1, buffer.length() - 3);
  }
  break;
  }
  return resp;
}