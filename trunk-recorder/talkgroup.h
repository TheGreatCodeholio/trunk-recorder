#ifndef TALKGROUP_H
#define TALKGROUP_H

#include <iostream>
#include <stdio.h>
#include <string>
#include "global_structs.h"
//#include <sstream>

class Talkgroup {
public:
  long number;
  std::string mode;
  std::string alpha_tag;
  std::string description;
  std::string tag;
  std::string group;
  int priority;
  int sys_num;
  double squelch_db;
  bool signal_detection;


  // For Conventional
  double freq;
  double tone;

  double timeout_time;

  Talkgroup(int sys_num, long num, std::string mode, std::string alpha_tag, std::string description, std::string tag, std::string group, int priority, unsigned long preferredNAC, double timeout_time = 0.0);
  Talkgroup(int sys_num, long num, double freq, double tone, std::string alpha_tag, std::string description, std::string tag, std::string group, double squelch_db, bool signal_detection, double timeout_time = 0.0);

  bool is_active();
  int get_priority();
  unsigned long get_preferredNAC();
  void set_priority(int new_priority);
  void set_active(bool a);
  std::string menu_string();

private:
  unsigned long preferredNAC;
  bool active;
};

#endif
