// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Shaochang Tan
// SPDX-FileCopyrightText: 2024-2025 Jason André Charles Gantner

#include "wdisplays.h"
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client-protocol.h>

#define MAX_NAME_LENGTH 256

struct profile_line {
  int start;
  int end;
};

typedef enum { Looking_for_profile, Looking_for_outputs, Found } parser_states;

static char *get_config_path(void) {
  const char *env_path = g_getenv("WDISPLAYS_KANSHI_CONFIG");
  if (env_path != NULL) {
    return g_strdup(env_path);
  }

  const char *config_dir = g_get_user_config_dir();
  g_autofree char *wdisplays_path = g_build_filename(config_dir, "wdisplays.conf", NULL);
  g_autofree char *contents = NULL;
  g_autofree char *store_path = NULL;
  if (g_file_get_contents(wdisplays_path, &contents, NULL, NULL)) {
    g_auto(GStrv) lines = g_strsplit(contents, "\n", -1);
    for (char **line = lines; *line != NULL; line++) {
      char *value = strchr(*line, '=');
      if (strstr(*line, "store_path") != NULL && value != NULL) {
        g_free(store_path);
        store_path = g_strdup(g_strstrip(value + 1));
      }
    }
  }
  if (store_path != NULL && store_path[0] != '\0') {
    return g_steal_pointer(&store_path);
  }
  return g_build_filename(config_dir, "kanshi", "config", NULL);
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
#ifdef VERBOSE
  char *profileName;
#endif
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
          #ifdef VERBOSE
          pstart     += 7;
          char *pend  = strchr(pstart, '{'); // find the end of the profile name
          while (isspace(*pend)) pend--;
          size_t pnsize    = pend - pstart;
          // use strndup to extract it without being size constrained
          profileName      = strndup(pstart, pnsize);
          #endif
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
          char outputName[MAX_NAME_LENGTH];
          char *trimmedBuffer = buffer;
          while (isspace(*trimmedBuffer)) {
            trimmedBuffer++; // skip leading spaces
          }
          char tempName[MAX_NAME_LENGTH];
          int matched_scan = 0;

          // Try quoted format first (legacy): output "Long Description (DP-3)"
          if (sscanf(trimmedBuffer, "output \"%255[^\"]\"", tempName) == 1) {
            // Extract output name from parentheses if present: (DP-3) -> DP-3
            char *paren_start = strrchr(tempName, '(');
            char *paren_end = strrchr(tempName, ')');
            if (paren_start && paren_end && paren_end > paren_start) {
              size_t len = paren_end - paren_start - 1;
              strncpy(outputName, paren_start + 1, len);
              outputName[len] = '\0';
              matched_scan = 1;
            }
          } else if (sscanf(trimmedBuffer, "output %99s", outputName) == 1) {
            // Try unquoted format: output DP-3
            matched_scan = 1;
          }
          
          if (matched_scan != 1) continue; // Skip unparseable lines

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
            ps                = Looking_for_profile;
          }
        }
        break;
    }
  }
  fclose(configFile);
  if (ps == Found) {

    #ifdef VERBOSE
    printf("Matched profile:%s\n", profileName);
    printf("Start line:%d\nEnd line:%d\n", profileStartLine, profileEndLine);
    #endif
    matched_profile.start = profileStartLine;
    matched_profile.end   = profileEndLine;
  } else dprintf(2, "%s:%i:%s(): Cannot find existing profile to match\n", __FILE__, __LINE__, __func__);
  return matched_profile;
}

static const char *transform_name(enum wl_output_transform transform) {
  switch (transform) {
  case WL_OUTPUT_TRANSFORM_90:
    return "90";
  case WL_OUTPUT_TRANSFORM_180:
    return "180";
  case WL_OUTPUT_TRANSFORM_270:
    return "270";
  case WL_OUTPUT_TRANSFORM_FLIPPED:
    return "flipped";
  case WL_OUTPUT_TRANSFORM_FLIPPED_90:
    return "flipped-90";
  case WL_OUTPUT_TRANSFORM_FLIPPED_180:
    return "flipped-180";
  case WL_OUTPUT_TRANSFORM_FLIPPED_270:
    return "flipped-270";
  default:
    return "normal";
  }
}

int wd_store_config(struct wl_list *outputs) {
  g_autofree char *file_name = get_config_path();
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

    const char *trans_str = transform_name(output->transform);

    if (description_index < HEADS_MAX) {
      descriptions[description_index] = strdup(head->name);
      if (!output->enabled) {
        sprintf(outputConfigs[description_index], "output %s disable", head->name);
      } else {
        char refresh[G_ASCII_DTOSTR_BUF_SIZE];
        char scale[G_ASCII_DTOSTR_BUF_SIZE];
        g_ascii_formatd(refresh, sizeof(refresh), "%.3f", output->refresh / 1.0e3);
        g_ascii_dtostr(scale, sizeof(scale), output->scale);
        sprintf(outputConfigs[description_index], "output %s enable position %d,%d mode %dx%d@%sHz scale %s transform %s",
                head->name, output->x, output->y, output->width, output->height, refresh, scale, trans_str);
      }
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
          dprintf(2, "%s:%i:%s(): too many outputs : %i", __FILE__, __LINE__, __func__, _i_output);
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
