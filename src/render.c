/* SPDX-FileCopyrightText: 2020 Jason Francis <jason@cycles.network>
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "hypr-displays.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <epoxy/gl.h>
#include <wayland-util.h>

#define BT_UV_VERT_SIZE (2 + 2)
#define BT_UV_QUAD_SIZE (6 * BT_UV_VERT_SIZE)
#define BT_UV_MAX (BT_COLOR_QUAD_SIZE * HEADS_MAX)

#define BT_COLOR_VERT_SIZE (2 + 4)
#define BT_COLOR_QUAD_SIZE (6 * BT_COLOR_VERT_SIZE)
#define BT_COLOR_MAX (BT_COLOR_QUAD_SIZE * HEADS_MAX)

#define BT_LINE_VERT_SIZE (2 + 4)
#define BT_LINE_QUAD_SIZE (8 * BT_LINE_VERT_SIZE)
#define BT_LINE_EXT_SIZE (24 * BT_LINE_VERT_SIZE)
#define BT_LINE_MAX (BT_LINE_EXT_SIZE * (HEADS_MAX + 1))

enum gl_buffers {
  TEXTURE_BUFFER,
  COLOR_BUFFER,
  LINE_BUFFER,
  NUM_BUFFERS
};

struct wd_gl_data {
  GLuint color_program;
  GLuint color_vertex_shader;
  GLuint color_fragment_shader;
  GLuint color_position_attribute;
  GLuint color_color_attribute;
  GLuint color_screen_size_uniform;

  GLuint texture_program;
  GLuint texture_vertex_shader;
  GLuint texture_fragment_shader;
  GLuint texture_position_attribute;
  GLuint texture_uv_attribute;
  GLuint texture_screen_size_uniform;
  GLuint texture_texture_uniform;
  GLuint texture_color_transform_uniform;

  GLuint buffers[NUM_BUFFERS];

  unsigned texture_count;
  GLuint textures[HEADS_MAX];

  float verts[BT_LINE_MAX];
};

static const char *color_vertex_shader_src = "\
precision mediump float;\n\
attribute vec2 position;\n\
attribute vec4 color;\n\
varying vec4 color_out;\n\
uniform vec2 screen_size;\n\
void main(void) {\n\
  vec2 screen_pos = (position / screen_size * 2. - 1.) * vec2(1., -1.);\n\
  gl_Position = vec4(screen_pos, 0., 1.);\n\
  color_out = color;\n\
}";

static const char *color_fragment_shader_src = "\
precision mediump float;\n\
varying vec4 color_out;\n\
void main(void) {\n\
  gl_FragColor = color_out;\n\
}";

static const char *texture_vertex_shader_src = "\
precision mediump float;\n\
attribute vec2 position;\n\
attribute vec2 uv;\n\
varying vec2 uv_out;\n\
uniform vec2 screen_size;\n\
void main(void) {\n\
  vec2 screen_pos = (position / screen_size * 2. - 1.) * vec2(1., -1.);\n\
  gl_Position = vec4(screen_pos, 0., 1.);\n\
  uv_out = uv;\n\
}";

static const char *texture_fragment_shader_src = "\
precision mediump float;\n\
varying vec2 uv_out;\n\
uniform sampler2D texture;\n\
uniform mat4 color_transform;\n\
void main(void) {\n\
  gl_FragColor = texture2D(texture, uv_out) * color_transform;\n\
}";

static GLuint gl_make_shader(GLenum type, const char *src) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &src, NULL);
  glCompileShader(shader);
  GLint status;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE) {
    GLsizei length;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    GLchar *log = "Failed";
    if (length > 0) {
      log = malloc(length);
      glGetShaderInfoLog(shader, length, NULL, log);
    }
    fprintf(stderr, "glCompileShader: %s\n", log);
    if (length > 0) {
      free(log);
    }
  }
  return shader;
}

static void gl_link_and_validate(GLint program) {
  GLint status;

  glLinkProgram(program);
  glGetProgramiv(program, GL_LINK_STATUS, &status);
  if (status == GL_FALSE) {
    GLsizei length;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    GLchar *log = malloc(length);
    glGetProgramInfoLog(program, length, NULL, log);
    fprintf(stderr, "glLinkProgram: %s\n", log);
    free(log);
    return;
  }
  glValidateProgram(program);
  glGetProgramiv(program, GL_VALIDATE_STATUS, &status);
  if (status == GL_FALSE) {
    GLsizei length;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    GLchar *log = malloc(length);
    glGetProgramInfoLog(program, length, NULL, log);
    fprintf(stderr, "glValidateProgram: %s\n", log);
    free(log);
  }
}

struct wd_gl_data *wd_gl_setup(void) {
  struct wd_gl_data *res = calloc(1, sizeof(struct wd_gl_data));
  res->color_program = glCreateProgram();

  res->color_vertex_shader = gl_make_shader(GL_VERTEX_SHADER,
      color_vertex_shader_src);
  glAttachShader(res->color_program, res->color_vertex_shader);
  res->color_fragment_shader = gl_make_shader(GL_FRAGMENT_SHADER,
      color_fragment_shader_src);
  glAttachShader(res->color_program, res->color_fragment_shader);
  gl_link_and_validate(res->color_program);

  res->color_position_attribute = glGetAttribLocation(res->color_program,
      "position");
  res->color_color_attribute = glGetAttribLocation(res->color_program,
      "color");
  res->color_screen_size_uniform = glGetUniformLocation(res->color_program,
      "screen_size");

  res->texture_program = glCreateProgram();

  res->texture_vertex_shader = gl_make_shader(GL_VERTEX_SHADER,
      texture_vertex_shader_src);
  glAttachShader(res->texture_program, res->texture_vertex_shader);
  res->texture_fragment_shader = gl_make_shader(GL_FRAGMENT_SHADER,
      texture_fragment_shader_src);
  glAttachShader(res->texture_program, res->texture_fragment_shader);
  gl_link_and_validate(res->texture_program);

  res->texture_position_attribute = glGetAttribLocation(res->texture_program,
      "position");
  res->texture_uv_attribute = glGetAttribLocation(res->texture_program,
      "uv");
  res->texture_screen_size_uniform = glGetUniformLocation(res->texture_program,
      "screen_size");
  res->texture_texture_uniform = glGetUniformLocation(res->texture_program,
      "texture");
  res->texture_color_transform_uniform = glGetUniformLocation(
      res->texture_program, "color_transform");

  glGenBuffers(NUM_BUFFERS, res->buffers);
  glBindBuffer(GL_ARRAY_BUFFER, res->buffers[TEXTURE_BUFFER]);
  glBufferData(GL_ARRAY_BUFFER, BT_UV_MAX * sizeof(float),
      NULL, GL_DYNAMIC_DRAW);

  glBindBuffer(GL_ARRAY_BUFFER, res->buffers[COLOR_BUFFER]);
  glBufferData(GL_ARRAY_BUFFER, BT_COLOR_MAX * sizeof(float),
      NULL, GL_DYNAMIC_DRAW);

  glBindBuffer(GL_ARRAY_BUFFER, res->buffers[LINE_BUFFER]);
  glBufferData(GL_ARRAY_BUFFER, BT_LINE_MAX * sizeof(float),
      NULL, GL_DYNAMIC_DRAW);

  return res;
}

static const GLfloat TRANSFORM_RGB[16] = {
  1, 0, 0, 0,
  0, 1, 0, 0,
  0, 0, 1, 0,
  0, 0, 0, 1};

static const GLfloat TRANSFORM_BGR[16] = {
  0, 0, 1, 0,
  0, 1, 0, 0,
  1, 0, 0, 0,
  0, 0, 0, 1};

#define PUSH_POINT_COLOR(_start, _a, _b, _color, _alpha) \
    *((_start)++) = (_a);\
    *((_start)++) = (_b);\
    *((_start)++) = ((_color)[0]);\
    *((_start)++) = ((_color)[1]);\
    *((_start)++) = ((_color)[2]);\
    *((_start)++) = (_alpha);

#define PUSH_POINT_UV(_start, _a, _b, _c, _d) \
    *((_start)++) = (_a);\
    *((_start)++) = (_b);\
    *((_start)++) = (_c);\
    *((_start)++) = (_d);

static inline float lerp(float x, float y, float a) {
  return x * (1.f - a) + y * a;
}

static inline void lerp_color(float out[3], float x[3], float y[3], float a) {
  out[0] = lerp(x[0], y[0], a);
  out[1] = lerp(x[1], y[1], a);
  out[2] = lerp(x[2], y[2], a);
  out[3] = lerp(x[3], y[3], a);
}

static inline float ease(float d) {
  d *= 2.f;
  if (d <= 1.f) {
    d = d * d;
  } else {
    d -= 1.f;
    d = d * (2.f - d) + 1.f;
  }
  d /= 2.f;
  return d;
}

void wd_gl_render(struct wd_gl_data *res, struct wd_render_data *info,
    uint64_t tick) {
  unsigned int tri_verts = 0;

  unsigned int head_count = wl_list_length(&info->heads);
  if (head_count >= HEADS_MAX)
    head_count = HEADS_MAX;

  if (head_count > res->texture_count) {
    glGenTextures(head_count - res->texture_count,
        res->textures + res->texture_count);
    for (int i = res->texture_count; i < head_count; i++) {
      glBindTexture(GL_TEXTURE_2D, res->textures[i]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
          GL_LINEAR_MIPMAP_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    res->texture_count = head_count;
  }

  struct wd_render_head_data *head;
  int i = 0;
  wl_list_for_each_reverse(head, &info->heads, link) {
    float *tri_ptr = res->verts + i * BT_UV_QUAD_SIZE;
    float x1 = head->active.x_invert ? head->x2 : head->x1;
    float y1 = head->y_invert ? head->y2 : head->y1;
    float x2 = head->active.x_invert ? head->x1 : head->x2;
    float y2 = head->y_invert ? head->y1 : head->y2;

    float sa = 0.f;
    float sb = 1.f;
    float sc = sb;
    float sd = sa;
    float ta = 0.f;
    float tb = ta;
    float tc = 1.f;
    float td = tc;
    for (int i = 0; i < head->active.rotation; i++) {
      float tmp = sd;
      sd = sc;
      sc = sb;
      sb = sa;
      sa = tmp;

      tmp = td;
      td = tc;
      tc = tb;
      tb = ta;
      ta = tmp;
    }

    PUSH_POINT_UV(tri_ptr, x1, y1, sa, ta)
    PUSH_POINT_UV(tri_ptr, x2, y1, sb, tb)
    PUSH_POINT_UV(tri_ptr, x1, y2, sd, td)
    PUSH_POINT_UV(tri_ptr, x1, y2, sd, td)
    PUSH_POINT_UV(tri_ptr, x2, y1, sb, tb)
    PUSH_POINT_UV(tri_ptr, x2, y2, sc, tc)

    tri_verts += 6;
    i++;
    if (i >= HEADS_MAX)
      break;
  }

  glClearColor(info->bg_color[0], info->bg_color[1], info->bg_color[2], 1.f);
  glClear(GL_COLOR_BUFFER_BIT);

  float screen_size[2] = { info->viewport_width, info->viewport_height };

  if (tri_verts > 0) {
    glUseProgram(res->texture_program);
    glBindBuffer(GL_ARRAY_BUFFER, res->buffers[TEXTURE_BUFFER]);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
        tri_verts * BT_UV_VERT_SIZE * sizeof(float), res->verts);
    glEnableVertexAttribArray(res->texture_position_attribute);
    glEnableVertexAttribArray(res->texture_uv_attribute);
    glVertexAttribPointer(res->texture_position_attribute,
        2, GL_FLOAT, GL_FALSE,
        BT_UV_VERT_SIZE * sizeof(float), (void *) (0 * sizeof(float)));
    glVertexAttribPointer(res->texture_uv_attribute, 2, GL_FLOAT, GL_FALSE,
        BT_UV_VERT_SIZE * sizeof(float), (void *) (2 * sizeof(float)));
    glUniform2fv(res->texture_screen_size_uniform, 1, screen_size);
    glUniform1i(res->texture_texture_uniform, 0);
    glActiveTexture(GL_TEXTURE0);

    i = 0;
    wl_list_for_each_reverse(head, &info->heads, link) {
      glBindTexture(GL_TEXTURE_2D, res->textures[i]);
      if (head->updated_at == tick) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, head->tex_stride / 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
            head->tex_width, head->tex_height,
            0, GL_RGBA, GL_UNSIGNED_BYTE, head->pixels);
        glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
        glGenerateMipmap(GL_TEXTURE_2D);
      }
      glUniformMatrix4fv(res->texture_color_transform_uniform, 1, GL_FALSE,
        head->swap_rgb ? TRANSFORM_RGB : TRANSFORM_BGR);
      glDrawArrays(GL_TRIANGLES, i * 6, 6);
      i++;
      if (i >= HEADS_MAX)
        break;
    }
  }

  tri_verts = 0;

  int j = 0;
  i = 0;
  bool any_clicked = false;
  uint64_t click_begin = 0;
  wl_list_for_each_reverse(head, &info->heads, link) {
    any_clicked = head->clicked || any_clicked;
    if (head->click_begin > click_begin)
      click_begin = head->click_begin;
    if (head->hovered || tick < head->hover_begin + HOVER_USECS) {
      float *tri_ptr = res->verts + j++ * BT_COLOR_QUAD_SIZE;
      float x1 = head->x1;
      float y1 = head->y1;
      float x2 = head->x2;
      float y2 = head->y2;

      float *color = info->selection_color;
      float d = fminf(
          (tick - head->hover_begin) / (double) HOVER_USECS, 1.f);
      if (!head->hovered)
        d = 1.f - d;
      float alpha = color[3] * ease(d) * .5f;

      PUSH_POINT_COLOR(tri_ptr, x1, y1, color, alpha)
      PUSH_POINT_COLOR(tri_ptr, x2, y1, color, alpha)
      PUSH_POINT_COLOR(tri_ptr, x1, y2, color, alpha)
      PUSH_POINT_COLOR(tri_ptr, x1, y2, color, alpha)
      PUSH_POINT_COLOR(tri_ptr, x2, y1, color, alpha)
      PUSH_POINT_COLOR(tri_ptr, x2, y2, color, alpha)

      tri_verts += 6;
    }
    i++;
    if (i >= HEADS_MAX)
      break;
  }

  if (tri_verts > 0) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(res->color_program);
    glBindBuffer(GL_ARRAY_BUFFER, res->buffers[COLOR_BUFFER]);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
        tri_verts * BT_COLOR_VERT_SIZE * sizeof(float), res->verts);
    glEnableVertexAttribArray(res->color_position_attribute);
    glEnableVertexAttribArray(res->color_color_attribute);
    glVertexAttribPointer(res->color_position_attribute, 2, GL_FLOAT, GL_FALSE,
        BT_COLOR_VERT_SIZE * sizeof(float), (void *) (0 * sizeof(float)));
    glVertexAttribPointer(res->color_color_attribute, 4, GL_FLOAT, GL_FALSE,
        BT_COLOR_VERT_SIZE * sizeof(float), (void *) (2 * sizeof(float)));
    glUniform2fv(res->color_screen_size_uniform, 1, screen_size);
    glDrawArrays(GL_TRIANGLES, 0, tri_verts);
    glDisable(GL_BLEND);
  }

  unsigned int line_verts = 0;
  i = 0;
  float *line_ptr = res->verts;
  if (any_clicked || (click_begin && tick < click_begin + HOVER_USECS)) {
    const float ox = -info->scroll_x - info->x_origin;
    const float oy = -info->scroll_y - info->y_origin;
    const float sx = screen_size[0];
    const float sy = screen_size[1];

    float color[4];
    lerp_color(color, info->selection_color, info->fg_color, .5f);
    float d = fminf(
        (tick - click_begin) / (double) HOVER_USECS, 1.f);
    if (!any_clicked)
      d = 1.f - d;
    float alpha = color[3] * ease(d) * .5f;

    PUSH_POINT_COLOR(line_ptr, ox, oy, color, alpha)
    PUSH_POINT_COLOR(line_ptr, sx, oy, color, alpha)
    PUSH_POINT_COLOR(line_ptr, ox, oy, color, alpha)
    PUSH_POINT_COLOR(line_ptr, ox, sy, color, alpha)

    line_verts += 4;
  }
  wl_list_for_each(head, &info->heads, link) {
    float x1 = head->x1;
    float y1 = head->y1;
    float x2 = head->x2;
    float y2 = head->y2;

    float *color = info->fg_color;
    float alpha = color[3] * (head->clicked ? .5f : .25f);

    PUSH_POINT_COLOR(line_ptr, x1, y1, color, alpha)
    PUSH_POINT_COLOR(line_ptr, x2, y1, color, alpha)
    PUSH_POINT_COLOR(line_ptr, x2, y1, color, alpha)
    PUSH_POINT_COLOR(line_ptr, x2, y2, color, alpha)
    PUSH_POINT_COLOR(line_ptr, x2, y2, color, alpha)
    PUSH_POINT_COLOR(line_ptr, x1, y2, color, alpha)
    PUSH_POINT_COLOR(line_ptr, x1, y2, color, alpha)
    PUSH_POINT_COLOR(line_ptr, x1, y1, color, alpha)

    line_verts += 8;

    if (any_clicked || (click_begin && tick < click_begin + HOVER_USECS)) {
      float d = fminf(
          (tick - click_begin) / (double) HOVER_USECS, 1.f);
      if (!any_clicked)
        d = 1.f - d;
      alpha = color[3] * ease(d) * (head->clicked ? .15f : .075f);

      const float sx = screen_size[0];
      const float sy = screen_size[1];

      PUSH_POINT_COLOR(line_ptr, 0,  y1, color, alpha)
      PUSH_POINT_COLOR(line_ptr, x1, y1, color, alpha)
      PUSH_POINT_COLOR(line_ptr, x1, 0,  color, alpha)
      PUSH_POINT_COLOR(line_ptr, x1, y1, color, alpha)

      PUSH_POINT_COLOR(line_ptr, sx, y1, color, alpha)
      PUSH_POINT_COLOR(line_ptr, x2, y1, color, alpha)
      PUSH_POINT_COLOR(line_ptr, x2, 0,  color, alpha)
      PUSH_POINT_COLOR(line_ptr, x2, y1, color, alpha)

      PUSH_POINT_COLOR(line_ptr, sx, y2, color, alpha)
      PUSH_POINT_COLOR(line_ptr, x2, y2, color, alpha)
      PUSH_POINT_COLOR(line_ptr, x2, sy, color, alpha)
      PUSH_POINT_COLOR(line_ptr, x2, y2, color, alpha)

      PUSH_POINT_COLOR(line_ptr, 0,  y2, color, alpha)
      PUSH_POINT_COLOR(line_ptr, x1, y2, color, alpha)
      PUSH_POINT_COLOR(line_ptr, x1, sy, color, alpha)
      PUSH_POINT_COLOR(line_ptr, x1, y2, color, alpha)

      line_verts += 16;
    }

    i++;
    if (i >= HEADS_MAX)
      break;
  }

  if (line_verts > 0) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(res->color_program);
    glBindBuffer(GL_ARRAY_BUFFER, res->buffers[LINE_BUFFER]);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
        line_verts * BT_LINE_VERT_SIZE * sizeof(float), res->verts);
    glEnableVertexAttribArray(res->color_position_attribute);
    glEnableVertexAttribArray(res->color_color_attribute);
    glVertexAttribPointer(res->color_position_attribute, 2, GL_FLOAT, GL_FALSE,
        BT_LINE_VERT_SIZE * sizeof(float), (void *) (0 * sizeof(float)));
    glVertexAttribPointer(res->color_color_attribute, 4, GL_FLOAT, GL_FALSE,
        BT_LINE_VERT_SIZE * sizeof(float), (void *) (2 * sizeof(float)));
    glUniform2fv(res->color_screen_size_uniform, 1, screen_size);
    glDrawArrays(GL_LINES, 0, line_verts);
    glDisable(GL_BLEND);
  }
}

void wd_gl_cleanup(struct wd_gl_data *res) {
  glDeleteBuffers(NUM_BUFFERS, res->buffers);
  glDeleteShader(res->texture_fragment_shader);
  glDeleteShader(res->texture_vertex_shader);
  glDeleteProgram(res->texture_program);

  glDeleteShader(res->color_fragment_shader);
  glDeleteShader(res->color_vertex_shader);
  glDeleteProgram(res->color_program);

  free(res);
}
