// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Shaochang Tan
// SPDX-FileCopyrightText: 2024-2025 Jason André Charles Gantner

#include "wdisplays.h"
#include <ctype.h>
#include <limits.h>
#include <regex.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client-protocol.h>

#define MAX_NAME_LENGTH 256

struct wd_head_config;

struct profile_line {
  int start;
  int end;
};

typedef enum { Looking_for_profile, Looking_for_outputs, Found } parser_states;

char *wd_get_config_file_path() {
  char kanshiConfigPath[PATH_MAX];
  char wdisplaysPath[PATH_MAX];
  char defaultConfigDir[PATH_MAX];
  // if $XDG_CONFIG_HOME is set, use it
  {
    char *configDir = getenv("XDG_CONFIG_HOME");
    if (configDir == NULL) { // fallback to $HOME
      configDir = getenv("HOME");
      if (configDir == NULL) {
        dprintf(2, "%s:%i:%s(): Cannot find $XDG_CONFIG_HOME nor $HOME directories", __FILE__, __LINE__, __func__);
        return NULL;
      } else { // configdir is $HOME/config
        snprintf(defaultConfigDir, sizeof(defaultConfigDir), "%s/.config", configDir);
      }
    } else { // configDir is $XDG_CONFIG_HOME
      snprintf(defaultConfigDir, sizeof(defaultConfigDir), "%s", configDir);
    }
  }

  // set  default kanshi config path
  snprintf(kanshiConfigPath, sizeof(kanshiConfigPath), "%s/kanshi/config", defaultConfigDir);

  // look for store_path in wdisplays.conf
  snprintf(wdisplaysPath, sizeof(wdisplaysPath), "%s/wdisplays.conf", defaultConfigDir);

  FILE *wdisplaysFile = fopen(wdisplaysPath, "r");
  if (wdisplaysFile != NULL) {
    char line[LINE_MAX]; // LINE_MAX is a platform-dependendant macro

    // try to match "store_path" term
    while (fgets(line, sizeof(line), wdisplaysFile) != NULL) {
      if (strstr(line, "store_path") != NULL) {
        // if found, extract path
        char *pathStart = strchr(line, '=');
        if (pathStart != NULL) {
          pathStart++;                             // skip '='
          while (isspace(*pathStart)) pathStart++; // skip spaces between '=' and the start of the path
          char *pathEnd = strchr(pathStart, '\n');
          size_t pathLen;
          if (pathEnd != NULL) pathLen = pathEnd - pathStart;
          else // store_path= is the last line and there's no newline at the end of the file
            pathLen = strnlen(pathStart, PATH_MAX);
          // save path
          strncpy(kanshiConfigPath, pathStart, pathLen);
        } else
          ; // store_path was not followed by an equal sign on this line
      } else
        ; // this line does not contain store_path
    }     // reached end of file
    fclose(wdisplaysFile);
  } else { // can't open config file
    dprintf(2, "%s:%i:%s(): Can't open %s : ", __FILE__, __LINE__, __func__, wdisplaysPath);
    perror(NULL);
  }

  // look for WDISPLAYS_KANSHI_CONFIG
  {
    char *envKanshiConf = getenv("WDISPLAYS_KANSHI_CONFIG");
    if (envKanshiConf != NULL) strncpy(kanshiConfigPath, envKanshiConf, sizeof(kanshiConfigPath));
    else
      ;
  }
  char *finalPath = strndup(kanshiConfigPath, PATH_MAX);
  if (finalPath == NULL) {
    dprintf(2, "%s:%i:%s(): ", __FILE__, __LINE__, __func__);
    perror("Failed to allocate memory for kanshi config path");
  }
  return finalPath;
}

struct profile_line match(char **descriptions, int num, const char *filename) {
  struct profile_line matched_profile;
  matched_profile.start = -1;
  matched_profile.end   = -1;
  // -1 means not found
  FILE *configFile      = fopen(filename, "r");
  if (configFile == NULL) {
    dprintf(2, "%s:%i:%s(): Can't open %s : ", __FILE__, __LINE__, __func__, filename);
    perror(NULL);
    return matched_profile;
  }
  // buffer to store each line
  char buffer[LINE_MAX];
  char *profileName;
  int profileStartLine = 0; // mark the start line of matched profile
  int profileEndLine   = 0; // mark the end line of matched profile

  int lineCount              = 0;                   // current line number
  uint32_t profileMatchedNum = 0;                   // current number of matched outputs
  parser_states ps           = Looking_for_profile; // current state of the parser
  while (ps != Found && fgets(buffer, sizeof(buffer), configFile) != NULL) {
    lineCount++;
    switch (ps) {
      case Found: break; // unreachable code

      case Looking_for_profile:;
        // check if "profile" keyword is in the line and remember its position
        char *pstart = strstr(buffer, "profile ");
        if (pstart != NULL) {
          pstart     += 7;
          char *pend  = strchr(pstart, '{'); // find the end of the profile name
          while (isspace(*pend)) pend--;
          size_t pnsize    = pend - pstart;
          // use strndup to extract it without being size constrained
          profileName      = strndup(pstart, pnsize);
          // record the start line of the profile
          profileStartLine = lineCount;
          ps               = Looking_for_outputs;
        }
        break;

      case Looking_for_outputs:
        // check if the profile ends
        if (buffer[0] == '}') {
          profileEndLine = lineCount;
          if (profileMatchedNum == num) ps = Found;
        } else {
          char *on_start = strstr(buffer, "output");
          on_start       = strchr(on_start, '"');
          on_start++;
          char *on_end     = strchr(on_start, '"');
          char *outputName = strndup(on_start, on_end - on_start);
          // check if the output name is in the descriptions
          int i            = 0;
          while (descriptions[i] != NULL && strcmp(outputName, descriptions[i])) i++;
          if (descriptions[i] != NULL) {
            profileMatchedNum++;
          } else {
            // if any output is not matched, break
            profileMatchedNum = 0;
            ps                = Looking_for_profile;
          }
        }
        break;
    }
  }
  fclose(configFile);
  if (ps == Found) {
    printf("Matched profile:%s\n", profileName);
    printf("Start line:%d\nEnd line:%d\n", profileStartLine, profileEndLine);
    matched_profile.start = profileStartLine;
    matched_profile.end   = profileEndLine;
  } else dprintf(2, "%s:%i:%s(): Cannot find existing profile to match\n", __FILE__, __LINE__, __func__);
  return matched_profile;
}

int wd_store_config(struct wl_list *outputs) {
  const char *file_name = wd_get_kanshi_config();
  char tmp_file_name[PATH_MAX];
  sprintf(tmp_file_name, "%s.tmp", file_name);

  char *descriptions[HEADS_MAX];
  for (int i = 0; i < HEADS_MAX; i++) descriptions[i] = NULL;

  char *outputConfigs[HEADS_MAX];
  for (int i = 0; i < HEADS_MAX; i++) outputConfigs[i] = (char *)malloc(MAX_NAME_LENGTH);

  struct wd_head_config *output;
  int description_index = 0;
  wl_list_for_each(output, outputs, link) {
    struct wd_head *head = output->head;

    // for transform
    char *trans_str;
    switch (output->transform) {
      case WL_OUTPUT_TRANSFORM_NORMAL     : trans_str = "normal";
      case WL_OUTPUT_TRANSFORM_90         : trans_str = "90";
      case WL_OUTPUT_TRANSFORM_180        : trans_str = "180";
      case WL_OUTPUT_TRANSFORM_270        : trans_str = "270";
      case WL_OUTPUT_TRANSFORM_FLIPPED_90 : trans_str = "flipped-90";
      case WL_OUTPUT_TRANSFORM_FLIPPED_180: trans_str = "flipped-180";
      case WL_OUTPUT_TRANSFORM_FLIPPED_270: trans_str = "flipped-270";
      default                             : trans_str = "normal";
    };

    if (description_index < HEADS_MAX) {
      descriptions[description_index] = strdup(head->description);
      // write output config in given format
      sprintf(outputConfigs[description_index], "output \"%s\" position %d,%d mode %dx%d@%.4f scale %.2f transform %s",
              head->description, output->x, output->y, output->width, output->height, output->refresh / 1.0e3, output->scale,
              trans_str);
      description_index++;
    } else {
      dprintf(2, "Too many monitor!\n\t%i is the maximum allowed number", HEADS_MAX);
      return 1;
    }
  }

  int num_of_monitors = description_index;

  struct profile_line matched_profile;
  matched_profile = match(descriptions, num_of_monitors, file_name);

  if (matched_profile.start == -1) {
    // append new profile
    FILE *file = fopen(file_name, "a");
    if (file == NULL) {
      dprintf(2, "%s:%i:%s(): Can't open %s : ", __FILE__, __LINE__, __func__, file_name);
      perror(NULL);
      return 1;
    }
    fprintf(file, "\nprofile {\n");
    for (int i = 0; i < num_of_monitors; i++) {
      fprintf(file, "    %s\n", outputConfigs[i]);
      free(outputConfigs[i]);
    }
    fprintf(file, "}");
    fclose(file);
  } else if (matched_profile.start < matched_profile.end) {
    // rewrite corresponding lines
    FILE *file = fopen(file_name, "r");
    if (file == NULL) {
      perror("File open failed.");
      return 1;
    }
    FILE *tmp = fopen(tmp_file_name, "w");
    if (tmp == NULL) {
      dprintf(2, "%s:%i:%s(): Can't create %s : ", __FILE__, __LINE__, __func__, tmp_file_name);
      perror(NULL);
      fclose(file);
      return 1;
    }
    char _buffer[LINE_MAX];
    int _line     = 0;
    int _i_output = 0;
    while (fgets(_buffer, sizeof(_buffer), file) != NULL) {
      if (_line >= matched_profile.start && _line < matched_profile.end - 1) {
        if (_i_output >= num_of_monitors) {
          perror("Null pointer");
          fclose(tmp);
          fclose(file);
          return 1;
        }
        fprintf(tmp, "    %s\n", outputConfigs[_i_output]);
        free(outputConfigs[_i_output]);

        _i_output++;
      } else {
        fprintf(tmp, "%s", _buffer);
      }
      _line++;
    }
    fclose(file);
    fclose(tmp);

    remove(file_name);
    rename(tmp_file_name, file_name);
  }

  return 0;
}
