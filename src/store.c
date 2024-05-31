#include "wdisplays.h"
#include <ctype.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <limits.h>
#include <stdbool.h>
#define MAX_NAME_LENGTH 256
#define MAX_MONITORS_NUM 10
struct wd_head_config;
struct profile_line {
  int start;
  int end;
};
char *get_config_file_path() {
    char defaultPath[PATH_MAX]; // platform based marco PATH_MAX    
    char wdisplaysPath[PATH_MAX];
    // if $XDG_CONFIG_HOME is set, use it
    {
      const char *configDir = getenv("XDG_CONFIG_HOME");
      char defaultConfigDir[PATH_MAX];
      if (configDir == NULL) {
        const char *homeDir = getenv("HOME");
        if (homeDir == NULL) {
          perror("Cannot find home directory");
          return NULL;
        }
        snprintf(defaultConfigDir, sizeof(defaultConfigDir), "%s/.config", homeDir);
      } else {
        snprintf(defaultConfigDir, sizeof(defaultConfigDir), "%s", configDir);
      }
      snprintf(defaultPath, sizeof(defaultPath), "%s/kanshi/config", defaultConfigDir);
      snprintf(wdisplaysPath, sizeof(wdisplaysPath), "%s/wdisplays/config", defaultConfigDir);
    }

    FILE *wdisplaysFile = fopen(wdisplaysPath, "r");
    if (wdisplaysFile != NULL) {
        char line[LINE_MAX]; // LINE_MAX is a platform based marco

        // try to match "store_path" term
        while (fgets(line, sizeof(line), wdisplaysFile) != NULL) {
            if (strstr(line, "store_path") != NULL) {
                // if found, extract path
                char *pathStart = strchr(line, '=');
                if (pathStart != NULL) {
                    pathStart++; // skip '='
                    char *pathEnd = strchr(pathStart, '\n');
                    if (pathEnd != NULL) {
                        *pathEnd = '\0'; // replace '\n' with '\0'
                        fclose(wdisplaysFile);
                        return strdup(pathStart); // return path
                    }
                }
            }
        }
        fclose(wdisplaysFile);
    }

    // if store_path is not found in wdisplays config file, return default path
    return strdup(defaultPath);
}

struct profile_line match(char **descriptions, int num, char *filename) {
  struct profile_line matched_profile;
  matched_profile.start = -1;
  matched_profile.end = -1;
  // -1 means not found
  FILE *configFile = fopen(filename, "r");
  if (configFile == NULL) {
    perror("File open failed.");
    return matched_profile;
  }
  // buffer to store each line
  char buffer[LINE_MAX];
  char profileName[MAX_NAME_LENGTH];
  int profileStartLine = 0; // mark the start line of matched profile
  int profileEndLine = 0;   // mark the end line of matched profile

  int lineCount = 0; // current line number

  while (fgets(buffer, sizeof(buffer), configFile) != NULL) {
    lineCount++;

    // check if "profile" keyword is in the line
    if (strstr(buffer, "profile") != NULL) {
      // extract profile name
      sscanf(buffer, "profile %s {", profileName);

      // the number of matched outputs
      uint32_t profileMatchedNum = 0;

      // record the start line of the profile
      profileStartLine = lineCount;

      while (fgets(buffer, sizeof(buffer), configFile) != NULL) {
        lineCount++;

        // check if the profile ends
        if (buffer[0] == '}') {
          profileEndLine = lineCount;
          break;
        }
        char outputName[MAX_NAME_LENGTH];
        // 从当前行提取输出名称
        char *trimmedBuffer = buffer;
        while (isspace(*trimmedBuffer)) {
          trimmedBuffer++; // skip leading spaces
        }
        sscanf(trimmedBuffer, "output \"%99[^\"]\"", outputName); // extract output name

        // check if the output name is in the descriptions
        bool matched = false;
        for (int i = 0; descriptions[i] != NULL; i++) {
          if (strcmp(outputName, descriptions[i]) == 0) {
            matched = true;
            profileMatchedNum++;
            break;
          }
        }

        if (!matched) {
          // if any output is not matched, break
          profileMatchedNum = 0;
          break;
        }
      }

      if (profileMatchedNum == num) {
        printf("Matched profile:%s\n", profileName);
        printf("Start line:%d\n", profileStartLine);
        matched_profile.start = profileStartLine;
        printf("End line:%d\n", profileEndLine);
        matched_profile.end = profileEndLine;

        fclose(configFile);
        return matched_profile;
      }
    }
  }

  fclose(configFile);
  printf("Cannot find existing profile to match\n");
  return matched_profile;
}

int store_config(struct wl_list *outputs) {
  char *file_name = get_config_file_path();
  char tmp_file_name[PATH_MAX];
  sprintf(tmp_file_name,"%s.tmp",file_name);

  char *descriptions[MAX_MONITORS_NUM];
  for (int i = 0; i < MAX_MONITORS_NUM; i++) {
    descriptions[i] = NULL;
  }

  char *outputConfigs[MAX_MONITORS_NUM];
  for (int i = 0; i < MAX_MONITORS_NUM; i++) {
    outputConfigs[i] = (char *)malloc(MAX_NAME_LENGTH);
  }

  struct wd_head_config *output;
  int description_index = 0;
  wl_list_for_each(output, outputs, link) {
    struct wd_head *head = output->head;

    // for transform
    char *trans_str = (char *)malloc(15 * sizeof(char));
    switch (output->transform) {
    case WL_OUTPUT_TRANSFORM_NORMAL:
      strcpy(trans_str, "normal");
      break;
    case WL_OUTPUT_TRANSFORM_90:
      strcpy(trans_str, "90");
      break;
    case WL_OUTPUT_TRANSFORM_180:
      strcpy(trans_str, "180");
      break;
    case WL_OUTPUT_TRANSFORM_270:
      strcpy(trans_str, "270");
      break;
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:
      strcpy(trans_str, "flipped-90");
      break;
    case WL_OUTPUT_TRANSFORM_FLIPPED_180:
      strcpy(trans_str, "flipped-180");
      break;
    case WL_OUTPUT_TRANSFORM_FLIPPED_270:
      strcpy(trans_str, "flipped-270");
      break;
    default:
      strcpy(trans_str, "normal");
      break;
    }

    if (description_index < MAX_MONITORS_NUM) {
      descriptions[description_index] = strdup(head->description);
      // write output config in given format
      sprintf(
          outputConfigs[description_index],
          "output \"%s\" position %d,%d mode %dx%d@%.4f scale %.2f transform %s",
          head->description, output->x, output->y, output->width,
          output->height, output->refresh / 1.0e3, output->scale, trans_str);
      description_index++;
    } else {
      free(trans_str);
      printf("Too many monitor! 10 is the");
      return 1;
    }

    free(trans_str);
  }

  int num_of_monitors = description_index;

  struct profile_line matched_profile;
  matched_profile = match(descriptions, num_of_monitors, file_name);

  if (matched_profile.start == -1) {
    // append new profile
    FILE *file = fopen(file_name, "a");
    if (file == NULL) {
      perror("File open failed.");
      free(file_name);
      return 1;
    }
    fprintf(file, "\nprofile {\n");
    for (int i = 0; i<num_of_monitors;  i++) {
      fprintf(file, "    %s\n", outputConfigs[i]);
      free(outputConfigs[i]);
    }
    fprintf(file, "}");
    fclose(file);
  } else if (matched_profile.start < matched_profile.end) {
    // rewrite correspondence lines
    FILE *file = fopen(file_name, "r");
    if (file == NULL) {
      perror("File open failed.");
      free(file_name);
      return 1;
    }
    FILE *tmp = fopen(tmp_file_name, "w");
    if (tmp == NULL) {
      perror("Tmp file cannot be created.");
      fclose(file);
      free(file_name);
      return 1;
    }
    char _buffer[LINE_MAX];
    int _line = 0;
    int _i_output = 0;
    while (fgets(_buffer, sizeof(_buffer), file) != NULL) {
      if (_line >= matched_profile.start && _line < matched_profile.end - 1) {
        if(_i_output>=num_of_monitors){
          perror("Null pointer");
          fclose(tmp);
          fclose(file);
          return 1;
        }
        fprintf(tmp,"    %s\n",outputConfigs[_i_output]);
        free(outputConfigs[_i_output]);

        _i_output++;
      } else{
        fprintf(tmp,"%s",_buffer);
      }
      _line++;
    }
    fclose(file);
    fclose(tmp);
    
    remove(file_name);
    rename(tmp_file_name, file_name);
    free(file_name);
  }

  return 0;
}