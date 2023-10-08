#include "wdisplays.h"
#include <ctype.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#define MAX_NAME_LENGTH 256
#define MAX_MONITORS_NUM 10
struct wd_head_config;
struct profile_line {
  int start;
  int end;
};
char *get_config_file_path() {
    // 获取用户的主目录路径
    const char *homeDir = getenv("HOME");

    if (homeDir == NULL) {
        perror("Cannot load $HOME env.");
        return NULL;
    }

    // 构建默认的配置文件路径
    char defaultPath[256]; // 假设文件路径不超过256个字符
    snprintf(defaultPath, sizeof(defaultPath), "%s/.config/kanshi/config", homeDir);

    // 尝试打开并读取 $HOME/.config/wdisplays/config 文件
    char wdisplaysPath[256];
    snprintf(wdisplaysPath, sizeof(wdisplaysPath), "%s/.config/wdisplays/config", homeDir);

    FILE *wdisplaysFile = fopen(wdisplaysPath, "r");
    if (wdisplaysFile != NULL) {
        char line[256]; // 假设行的长度不超过256个字符

        // 逐行读取文件，查找 "store PATH" 配置项
        while (fgets(line, sizeof(line), wdisplaysFile) != NULL) {
            if (strstr(line, "store_path") != NULL) {
                // 找到 "store PATH" 配置项，提取路径
                char *pathStart = strchr(line, '=');
                if (pathStart != NULL) {
                    pathStart++; // 跳过等号
                    char *pathEnd = strchr(pathStart, '\n');
                    if (pathEnd != NULL) {
                        *pathEnd = '\0'; // 去除换行符
                        fclose(wdisplaysFile);
                        return strdup(pathStart); // 返回提取的路径
                    }
                }
            }
        }

        fclose(wdisplaysFile);
    }

    // 如果没有找到 "store PATH" 配置项，则返回默认路径
    return strdup(defaultPath);
}

struct profile_line match(char **descriptions, int num, char *filename) {
  struct profile_line matched_profile;
  matched_profile.start = -1;
  matched_profile.end = -1;
  FILE *configFile = fopen(filename, "r");
  if (configFile == NULL) {
    perror("File open failed.");
    return matched_profile;
  }
  // 缓冲区用于存储文件行
  char buffer[1024];
  char profileName[MAX_NAME_LENGTH];
  int profileStartLine = 0; // 记录匹配到的profile的起始行号
  int profileEndLine = 0;   // 记录匹配到的profile的结束行号

  int lineCount = 0; // 用于记录当前行号

  while (fgets(buffer, sizeof(buffer), configFile) != NULL) {
    lineCount++; // 增加行号

    // 检查是否包含 "profile" 关键字
    if (strstr(buffer, "profile") != NULL) {
      // 从当前行提取 profile 名称
      sscanf(buffer, "profile %s {", profileName);

      // 标记当前 profile 是否匹配
      int profileMatched = 0;

      // 记录匹配到的profile的起始行号
      profileStartLine = lineCount;

      // 遍历 profile 中的输出行
      while (fgets(buffer, sizeof(buffer), configFile) != NULL) {
        lineCount++; // 增加行号

        // 检查是否到达当前 profile 的末尾
        if (buffer[0] == '}') {
          // 记录匹配到的profile的结束行号
          profileEndLine = lineCount;
          break; // 退出当前 profile
        }
        char outputName[MAX_NAME_LENGTH];
        // 从当前行提取输出名称
        char *trimmedBuffer = buffer;
        while (isspace(*trimmedBuffer)) {
          trimmedBuffer++;
        }
        sscanf(trimmedBuffer, "output \"%99[^\"]\"", outputName);

        // 检查是否匹配
        int matched = 0;
        for (int i = 0; descriptions[i] != NULL; i++) {
          if (strcmp(outputName, descriptions[i]) == 0) {
            matched = 1;
            profileMatched++;
            break;
          }
        }

        if (!matched) {
          // 如果有任何一个输出不匹配，则标记为不匹配
          profileMatched = 0;
          break;
        }
      }

      if (profileMatched == num) {
        printf("Matched profile：%s\n", profileName);
        printf("Start line：%d\n", profileStartLine);
        matched_profile.start = profileStartLine;
        printf("End line：%d\n", profileEndLine);
        matched_profile.end = profileEndLine;

        fclose(configFile);
        return matched_profile;
      }
    }
  }

  // 关闭配置文件
  fclose(configFile);

  printf("Cannot find exsiting profile to match\n");
  return matched_profile;
}

int store_config(struct wl_list *outputs) {
  char *file_name = get_config_file_path();
  char tmp_file_name[256];
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

      sprintf(
          outputConfigs[description_index],
          "output \"%s\" position %d,%d mode %dx%d@%.4f scale %.2f transform %s",
          head->description, output->x, output->y, output->width,
          output->height, output->refresh / 1.0e3, output->scale, trans_str);
      description_index++;
    } else {
      printf("Too many monitor!");
      return 1;
    }

    free(trans_str);
  }

  int num_of_monitors = description_index;

  struct profile_line matched_profile;
  matched_profile = match(descriptions, num_of_monitors, file_name);

  if (matched_profile.start == -1) {
    FILE *file = fopen(file_name, "a");
    if (file == NULL) {
      perror("File open failed.");
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
    // rewrite correspondece lines
    FILE *file = fopen(file_name, "r");
    if (file == NULL) {
      perror("File open failed.");
      return 1;
    }
    FILE *tmp = fopen(tmp_file_name, "w");
    if (tmp == NULL) {
      perror("Tmp file cannot be created.");
      fclose(file);
      return 1;
    }
    char _buffer[1024];
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
  }

  return 0;
}