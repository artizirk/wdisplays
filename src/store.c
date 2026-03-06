// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Shaochang Tan
// SPDX-FileCopyrightText: 2024-2025 Jason André Charles Gantner

#include <errno.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>

#include "wdisplays.h"

struct kanshi_directive {
  char *name;
  GPtrArray *params;
  GPtrArray *children;
  size_t start, name_start, end;
  size_t criteria_start, criteria_end;
};

struct kanshi_parser {
  const char *text;
  size_t pos;
  int line;
};

static char *get_config_path(void) {
  const char *env_path = g_getenv("WDISPLAYS_KANSHI_CONFIG");
  if (env_path != NULL && env_path[0] != '\0') {
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

static void kanshi_directive_free(gpointer data) {
  struct kanshi_directive *dir = data;
  g_free(dir->name);
  g_ptr_array_unref(dir->params);
  if (dir->children != NULL) {
    g_ptr_array_unref(dir->children);
  }
  g_free(dir);
}

static char peek(struct kanshi_parser *parser) {
  return parser->text[parser->pos];
}

static void skip_blanks(struct kanshi_parser *parser) {
  while (peek(parser) == ' ' || peek(parser) == '\t') {
    parser->pos++;
  }
}

static void next_line(struct kanshi_parser *parser) {
  while (peek(parser) != '\0' && peek(parser) != '\n') {
    parser->pos++;
  }
  if (peek(parser) == '\n') {
    parser->pos++;
    parser->line++;
  }
}

static bool parse_error(struct kanshi_parser *parser, GError **error,
    const char *message) {
  g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
      "line %d: %s", parser->line, message);
  return false;
}

static bool parse_word(struct kanshi_parser *parser, GString *word,
    GError **error) {
  char quote = peek(parser);
  if (quote == '"' || quote == '\'') {
    parser->pos++;
  } else {
    quote = '\0';
  }
  while (true) {
    char c = peek(parser);
    if (quote != '\0' && c == quote) {
      parser->pos++;
      return true;
    }
    if (c == '\0' || c == '\n') {
      if (quote != '\0') {
        return parse_error(parser, error, "unterminated quoted string");
      }
      return true;
    }
    if (quote == '\0' && (c == ' ' || c == '\t')) {
      return true;
    }
    if (quote == '\0' && strchr("\"'{}", c) != NULL) {
      return parse_error(parser, error, "unexpected character in word");
    }
    if (c == '\\' && quote != '\'') {
      parser->pos++;
      c = peek(parser);
      if (c == '\0' || c == '\n') {
        return parse_error(parser, error, "cannot escape a line break");
      }
    }
    g_string_append_c(word, c);
    parser->pos++;
  }
}

static GPtrArray *parse_block(struct kanshi_parser *parser, bool nested,
    GError **error);

static struct kanshi_directive *parse_directive(struct kanshi_parser *parser,
    GError **error) {
  struct kanshi_directive *dir = g_new0(struct kanshi_directive, 1);
  dir->params = g_ptr_array_new_with_free_func(g_free);
  dir->name_start = parser->pos;
  dir->start = parser->pos;
  while (dir->start > 0 && (parser->text[dir->start - 1] == ' '
        || parser->text[dir->start - 1] == '\t')) {
    dir->start--;
  }
  if (dir->start > 0 && parser->text[dir->start - 1] != '\n') {
    dir->start = dir->name_start;
  }

  g_autoptr(GString) word = g_string_new(NULL);
  if (!parse_word(parser, word, error)) {
    goto err;
  }
  dir->name = g_strdup(word->str);
  skip_blanks(parser);

  while (peek(parser) != '\0' && peek(parser) != '\n') {
    if (peek(parser) == '{') {
      parser->pos++;
      skip_blanks(parser);
      if (peek(parser) != '\n') {
        parse_error(parser, error, "expected a line break after '{'");
        goto err;
      }
      next_line(parser);
      dir->children = parse_block(parser, true, error);
      if (dir->children == NULL) {
        goto err;
      }
      dir->end = parser->pos;
      skip_blanks(parser);
      if (peek(parser) != '\0' && peek(parser) != '\n') {
        parser->pos = dir->end;
        return dir;
      }
      break;
    }
    if (peek(parser) == '}') {
      parse_error(parser, error, "unexpected '}'");
      goto err;
    }
    size_t param_start = parser->pos;
    g_string_truncate(word, 0);
    if (!parse_word(parser, word, error)) {
      goto err;
    }
    if (dir->params->len == 0) {
      dir->criteria_start = param_start;
      dir->criteria_end = parser->pos;
    }
    g_ptr_array_add(dir->params, g_strdup(word->str));
    skip_blanks(parser);
  }
  next_line(parser);
  dir->end = parser->pos;
  return dir;

err:
  kanshi_directive_free(dir);
  return NULL;
}

static GPtrArray *parse_block(struct kanshi_parser *parser, bool nested,
    GError **error) {
  GPtrArray *dirs = g_ptr_array_new_with_free_func(kanshi_directive_free);
  while (true) {
    skip_blanks(parser);
    char c = peek(parser);
    if (c == '\n' || c == '#') {
      next_line(parser);
    } else if (c == '\0') {
      if (nested) {
        parse_error(parser, error, "expected '}'");
        break;
      }
      return dirs;
    } else if (c == '}') {
      if (!nested) {
        parse_error(parser, error, "unexpected '}'");
        break;
      }
      parser->pos++;
      return dirs;
    } else {
      struct kanshi_directive *dir = parse_directive(parser, error);
      if (dir == NULL) {
        break;
      }
      g_ptr_array_add(dirs, dir);
    }
  }
  g_ptr_array_unref(dirs);
  return NULL;
}

static const char *find_param(struct kanshi_directive *dir, const char *key) {
  for (guint i = 1; i + 1 < dir->params->len; i++) {
    if (strcmp(g_ptr_array_index(dir->params, i), key) == 0) {
      return g_ptr_array_index(dir->params, i + 1);
    }
  }
  for (guint i = 0; dir->children != NULL && i < dir->children->len; i++) {
    struct kanshi_directive *child = g_ptr_array_index(dir->children, i);
    if (strcmp(child->name, key) == 0 && child->params->len > 0) {
      return g_ptr_array_index(child->params, 0);
    }
  }
  return NULL;
}

static bool is_output(struct kanshi_directive *dir) {
  return strcmp(dir->name, "output") == 0 && dir->params->len > 0;
}

static char *head_identifier(struct wd_head *head) {
  return g_strdup_printf("%s %s %s",
      head->make != NULL ? head->make : "Unknown",
      head->model != NULL ? head->model : "Unknown",
      head->serial_number != NULL ? head->serial_number : "Unknown");
}

static bool criteria_match(const char *criteria, struct wd_head *head) {
  g_autofree char *identifier = head_identifier(head);
  return strcmp(criteria, "*") == 0 || strcmp(criteria, head->name) == 0
    || fnmatch(criteria, identifier, 0) == 0;
}

static const char *resolve_alias(GPtrArray *config, const char *criteria) {
  if (criteria[0] != '$') {
    return criteria;
  }
  for (guint i = 0; i < config->len; i++) {
    struct kanshi_directive *dir = g_ptr_array_index(config, i);
    const char *alias = is_output(dir) ? find_param(dir, "alias") : NULL;
    if (alias != NULL && strcmp(alias, criteria) == 0) {
      return g_ptr_array_index(dir->params, 0);
    }
  }
  return NULL;
}

static bool match_profile(GPtrArray *config, struct kanshi_directive *profile,
    struct wd_head_config **heads, int num_heads,
    struct kanshi_directive **matches) {
  g_autoptr(GPtrArray) outputs = g_ptr_array_new();
  for (guint i = 0; i < profile->children->len; i++) {
    struct kanshi_directive *child = g_ptr_array_index(profile->children, i);
    if (!is_output(child)) {
      continue;
    }
    if (strcmp(g_ptr_array_index(child->params, 0), "*") == 0) {
      g_ptr_array_add(outputs, child);
    } else {
      g_ptr_array_insert(outputs, 0, child);
    }
  }
  if (outputs->len != (guint) num_heads) {
    return false;
  }

  memset(matches, 0, num_heads * sizeof(*matches));
  for (guint i = 0; i < outputs->len; i++) {
    struct kanshi_directive *output = g_ptr_array_index(outputs, i);
    const char *criteria = resolve_alias(config,
        g_ptr_array_index(output->params, 0));
    if (criteria == NULL) {
      return false;
    }
    int j = 0;
    while (j < num_heads
        && (matches[j] != NULL || !criteria_match(criteria, heads[j]->head))) {
      j++;
    }
    if (j == num_heads) {
      return false;
    }
    matches[j] = output;
  }
  return true;
}

static void append_word(GString *str, const char *word) {
  if (word[0] != '\0' && strpbrk(word, " \t\"'{}\\#") == NULL) {
    g_string_append(str, word);
    return;
  }
  g_string_append_c(str, '"');
  for (const char *c = word; *c != '\0'; c++) {
    if (*c == '"' || *c == '\\') {
      g_string_append_c(str, '\\');
    }
    g_string_append_c(str, *c);
  }
  g_string_append_c(str, '"');
}

static void append_criteria(GString *str, struct wd_head_config **heads,
    int num_heads, struct wd_head *head) {
  g_autofree char *identifier = head_identifier(head);
  bool usable = head->make != NULL || head->model != NULL
    || head->serial_number != NULL;
  if (identifier[0] == '$' || strchr(identifier, '\n') != NULL) {
    usable = false;
  }
  for (int i = 0; usable && i < num_heads; i++) {
    g_autofree char *other = head_identifier(heads[i]->head);
    if (heads[i]->head != head && strcmp(identifier, other) == 0) {
      usable = false;
    }
  }
  if (!usable) {
    append_word(str, head->name);
    return;
  }

  g_autoptr(GString) pattern = g_string_new(NULL);
  for (const char *c = identifier; *c != '\0'; c++) {
    if (strchr("*?[\\", *c) != NULL) {
      g_string_append_c(pattern, '\\');
    }
    g_string_append_c(pattern, *c);
  }
  append_word(str, pattern->str);
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

static bool is_custom_mode(struct wd_head_config *output) {
  struct wd_mode *mode;
  wl_list_for_each(mode, &output->head->modes, link) {
    if (mode->width == output->width && mode->height == output->height
        && mode->refresh == output->refresh) {
      return false;
    }
  }
  return true;
}

static void append_settings(GString *str, struct wd_head_config *output,
    const char *adaptive_sync) {
  if (!output->enabled) {
    g_string_append(str, " disable\n");
    return;
  }
  g_string_append(str, " enable mode ");
  if (is_custom_mode(output)) {
    g_string_append(str, "--custom ");
  }
  g_string_append_printf(str, "%dx%d", output->width, output->height);
  if (output->refresh > 0) {
    char refresh[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_formatd(refresh, sizeof(refresh), "%.3f", output->refresh / 1000.);
    g_string_append_printf(str, "@%sHz", refresh);
  }
  char scale[G_ASCII_DTOSTR_BUF_SIZE];
  g_ascii_dtostr(scale, sizeof(scale), output->scale);
  g_string_append_printf(str, " position %d,%d scale %s transform %s",
      output->x, output->y, scale, transform_name(output->transform));
  if (adaptive_sync != NULL) {
    g_string_append(str, " adaptive_sync ");
    append_word(str, adaptive_sync);
  }
  g_string_append_c(str, '\n');
}

static void rewrite_profile(GString *str, const char *text,
    struct kanshi_directive *profile, struct wd_head_config **heads,
    int num_heads, struct kanshi_directive **matches) {
  size_t pos = 0;
  for (guint i = 0; i < profile->children->len; i++) {
    struct kanshi_directive *child = g_ptr_array_index(profile->children, i);
    g_string_append_len(str, text + pos, child->start - pos);
    pos = child->end;
    if (!is_output(child)) {
      g_string_append_len(str, text + child->start, child->end - child->start);
      continue;
    }
    for (int j = 0; j < num_heads; j++) {
      if (matches[j] == child) {
        g_string_append_len(str, text + child->start,
            child->name_start - child->start);
        g_string_append(str, "output ");
        g_string_append_len(str, text + child->criteria_start,
            child->criteria_end - child->criteria_start);
        append_settings(str, heads[j], find_param(child, "adaptive_sync"));
      }
    }
  }
  g_string_append(str, text + pos);
}

static void append_profile(GString *str, struct wd_head_config **heads,
    int num_heads) {
  if (str->len > 0 && str->str[str->len - 1] != '\n') {
    g_string_append_c(str, '\n');
  }
  if (str->len > 0) {
    g_string_append_c(str, '\n');
  }
  g_string_append(str, "profile {\n");
  for (int i = 0; i < num_heads; i++) {
    g_string_append(str, "\toutput ");
    append_criteria(str, heads, num_heads, heads[i]->head);
    append_settings(str, heads[i], NULL);
  }
  g_string_append(str, "}\n");
}

static char *update_config(const char *text, struct wl_list *outputs,
    GError **error) {
  struct wd_head_config *heads[HEADS_MAX];
  int num_heads = 0;
  struct wd_head_config *output;
  wl_list_for_each_reverse(output, outputs, link) {
    if (num_heads == HEADS_MAX) {
      g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "too many outputs");
      return NULL;
    }
    heads[num_heads++] = output;
  }
  if (num_heads == 0) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "no outputs");
    return NULL;
  }

  struct kanshi_parser parser = { .text = text, .line = 1 };
  g_autoptr(GPtrArray) config = parse_block(&parser, false, error);
  if (config == NULL) {
    return NULL;
  }

  GString *str = g_string_new(NULL);
  struct kanshi_directive *matches[HEADS_MAX];
  for (guint i = 0; i < config->len; i++) {
    struct kanshi_directive *dir = g_ptr_array_index(config, i);
    if (strcmp(dir->name, "profile") == 0 && dir->children != NULL
        && match_profile(config, dir, heads, num_heads, matches)) {
      rewrite_profile(str, text, dir, heads, num_heads, matches);
      return g_string_free(str, FALSE);
    }
  }
  g_string_append(str, text);
  append_profile(str, heads, num_heads);
  return g_string_free(str, FALSE);
}

static bool write_config(const char *path, const char *contents,
    GError **error) {
  g_autofree char *real_path = realpath(path, NULL);
  if (real_path != NULL) {
    path = real_path;
  }
  g_autofree char *dir = g_path_get_dirname(path);
  if (g_mkdir_with_parents(dir, 0755) != 0) {
    int err = errno;
    g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(err),
        "%s: %s", dir, g_strerror(err));
    return false;
  }
  return g_file_set_contents(path, contents, -1, error);
}

static void reload_kanshi(void) {
  g_autofree char *kanshictl = g_find_program_in_path("kanshictl");
  if (kanshictl == NULL) {
    return;
  }
  char *argv[] = { kanshictl, "reload", NULL };
  g_autoptr(GError) error = NULL;
  if (!g_spawn_async(NULL, argv, NULL, 0, NULL, NULL, NULL, &error)) {
    fprintf(stderr, "Failed to run kanshictl: %s\n", error->message);
  }
}

void wd_store_config(struct wd_state *state, struct wl_list *outputs) {
  g_autofree char *path = get_config_path();
  g_autofree char *contents = NULL;
  g_autofree char *updated = NULL;
  g_autoptr(GError) error = NULL;
  if (!g_file_get_contents(path, &contents, NULL, &error)) {
    if (!g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
      goto err;
    }
    g_clear_error(&error);
    contents = g_strdup("");
  }
  updated = update_config(contents, outputs, &error);
  if (updated == NULL || !write_config(path, updated, &error)) {
    goto err;
  }
  reload_kanshi();
  return;

err:;
  g_autofree char *message = g_strdup_printf(
      "Could not save the kanshi config %s: %s", path, error->message);
  wd_ui_show_error(state, message);
}
