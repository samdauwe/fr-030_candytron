/* csl.h - the script machine of GenThree (CSL) - single-header C99 library (stb style)

   Candytron is a compiled script (default.pac) that runs on a small stack machine and calls natives to build bitmaps, meshes, materials, scenes and
   effect chains. This header is that machine: the loader of the packed script (cslrt.cpp, ScriptRuntime::PackedImport) and the interpreter
   (ScriptRuntime::Interpret), with the same words, the same stacks and the same limits. What it does not contain is the natives: they are
   registered by the host (csl_natives.h has the ones of the demo).

   The original is 32-bit code: object pointers live in 32-bit integers, and a native reads its arguments from the machine's stacks through an
   x86 calling-convention trick. Here an object is a *handle* (an index into the machine's table, 0 is the null object), a native has one uniform
   signature, and a string is the address of its text in the string pool: GT3_STRING_BASE + offset.

   The machine has three stacks: integers (32 bit, 16.16 fixed point or the bits of a float), objects, and return addresses. Floats travel
   through the integer stack as their IEEE bits; the words I2F and F2I convert. Everything the machine computes from floats (I2F, F2I, the
   natives' float maths) is meant to be done with the rounding mode of the original, round down: csl_natives.h and the session set it (see there).

   USAGE
     In exactly one translation unit:

         #define GT3_CSL_IMPLEMENTATION
         #include <candytron/core/csl.h>

   CONFIGURATION (before including the implementation)
     GT3_CSL_STATIC    make every API function static
*/
#ifndef GT3_CSL_H_INCLUDED
#define GT3_CSL_H_INCLUDED

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GT3_CSL_API
#ifdef GT3_CSL_STATIC
#define GT3_CSL_API static
#else
#define GT3_CSL_API extern
#endif
#endif

/* the limits of the original (cslrt.hpp) */
#define GT3_MAXGLOBAL 2048 /* global variables */
#define GT3_MAXLOCAL  512  /* local variables per call */
#define GT3_MAXCALLS  16   /* call depth */
#define GT3_MAXUSER   1024 /* user words (functions) */
#define GT3_MAXCODE   512  /* native ids */
#define GT3_MAXSTACK  256

/* the words of the machine (cslrt.hpp) */
enum {
  GT3_NOP = 0x00,
  GT3_EOF = 0x01,
  GT3_RTS = 0x02,
  GT3_I31 = 0x03,
  GT3_ADD = 0x04,
  GT3_SUB = 0x05,
  GT3_MUL = 0x06,
  GT3_DIV = 0x07,
  GT3_MOD = 0x08,
  GT3_MIN = 0x09,
  GT3_MAX = 0x0a,
  GT3_AND = 0x0b,
  GT3_OR = 0x0c,
  GT3_SIN = 0x0d,
  GT3_COS = 0x0e,
  GT3_GT = 0x0f,
  GT3_GE = 0x10,
  GT3_LT = 0x12,
  GT3_LE = 0x13,
  GT3_EQ = 0x14,
  GT3_NE = 0x15,
  GT3_DUP = 0x16,
  GT3_F2I = 0x17,
  GT3_I2F = 0x18,
  GT3_CODE = 0x20,
  GT3_USER = 0x21,
  GT3_FUNC = 0x22,
  GT3_STRING = 0x23,
  GT3_IF = 0x24,
  GT3_THEN = 0x25,
  GT3_ELSE = 0x26,
  GT3_DO = 0x27,
  GT3_WHILE = 0x28,
  GT3_REPEAT = 0x29,
  GT3_STORELOCI = 0x30,
  GT3_STORELOCO = 0x31,
  GT3_FETCHLOCI = 0x32,
  GT3_FETCHLOCO = 0x33,
  GT3_STOREGLOI = 0x34,
  GT3_STOREGLOO = 0x35,
  GT3_FETCHGLOI = 0x36,
  GT3_FETCHGLOO = 0x37,
  GT3_STOREMEMI = 0x38,
  GT3_STOREMEMO = 0x39,
  GT3_FETCHMEMI = 0x3a,
  GT3_FETCHMEMO = 0x3b
};

/* the script's pointers to strings: the machine pushes GT3_STRING_BASE + the offset into the string pool */
#define GT3_STRING_BASE 0x40000000

/* the class ids of the original (_types.hpp sCID_GEN*): what the trace prints and what the objects are */
enum {
  GT3_CID_MATERIAL = 0x01000008,
  GT3_CID_BITMAP = 0x01000009,
  GT3_CID_MESH = 0x0100000b,
  GT3_CID_SCENE = 0x0100000c,
  GT3_CID_MATPASS = 0x0100000d,
  GT3_CID_FXCHAIN = 0x0100000e,
  GT3_CID_MATRIX = 0x0100000f,
  GT3_CID_SPLINE = 0x01000010,
  GT3_CID_MESHANIM = 0x01000011,
  GT3_CID_LIGHT = 0x01000012
};

/* ---- the program ---- */

typedef struct gt3_program {
  uint32_t *words;           /* word 0 is unused, as in the original */
  uint32_t count;
  int32_t user[GT3_MAXUSER]; /* index of the FUNC word of every user word, 0: none */
  uint8_t *strings;          /* the string pool, a copy */
  uint32_t string_bytes;
} gt3_program;

/* Decodes a packed script (the layout is in the header comment of cslrt.cpp). Returns 1, or 0 with the reason in `error`. */
GT3_CSL_API int gt3_program_load(gt3_program *p, const void *pac, size_t size, char *error, size_t error_size);
GT3_CSL_API void gt3_program_free(gt3_program *p);

/* ---- objects ---- */

#define GT3_MEMBERS 16 /* script-visible members (the script reaches 0..14) */
#define GT3_LINKS   40 /* a mesh: Material (1) + up to 32 anim labels, with some room */

/* One object of the machine. `m` are the members the script reads and writes with STOREMEMI/FETCHMEMI: the 32-bit slots that follow the object header
   in the original's class (for a scene the nine floats of SRT, then Multiply; for an effect chain Color, Zoom, Rotate, Center, CamPos, CamRot ...).
   `link` are the references an object holds to others, which FindLabel walks; their meaning depends on the class (csl_natives.h). */
typedef struct gt3_object {
  uint32_t cls;
  int32_t label; /* _Label of the original */
  int32_t m[GT3_MEMBERS];
  int32_t link[GT3_LINKS];
  int32_t *child; /* a scene's children (handles) */
  uint32_t n_child, cap_child;
  void *data;     /* class specific, owned: free()d with the object */
  int32_t trace_serial;
  /* mesh bookkeeping that decides whether AnimLabel creates an object */
  int32_t store_mode, anim_last_op, anim_count;
} gt3_object;

/* ---- the machine ---- */

struct gt3_vm;
/* A native reads its object arguments (handles) and integer arguments, in the order of the script's signature; it returns an integer or a handle, as
   its registration says. A native with return kind 0 may push results on the integer stack with gt3_push_i (the splines do). */
typedef int32_t (*gt3_native_fn)(struct gt3_vm *vm, const int32_t *objs, const int32_t *ints);

typedef struct gt3_native {
  gt3_native_fn fn;        /* NULL: the word does nothing, and takes no arguments off the stacks (the original's LoadSong and PlaySong in the intro) */
  uint8_t ints, objs, ret; /* ret: 0 nothing, 1 integer, 2 object */
  const char *name;
} gt3_native;

typedef struct gt3_vm {
  const gt3_program *program;
  int32_t x[GT3_MAXGLOBAL + GT3_MAXLOCAL * GT3_MAXCALLS]; /* globals, then the locals of every call level: integers and object handles alike */
  int32_t x_index;
  int32_t istack[GT3_MAXSTACK + 1], ostack[GT3_MAXSTACK + 1], rstack[GT3_MAXCALLS + 1];
  int32_t i_index, o_index, r_index;
  int32_t pc;
  int error;
  const char *error_message;
  gt3_native native[GT3_MAXCODE];

  gt3_object **object; /* handle h is object[h - 1] */
  uint32_t n_objects, cap_objects;

  uint32_t member_stores; /* STOREMEMI executed: the script's writes into object members (statistics of the tests) */
  uint32_t progress;      /* calls to natives while ShowProgress was set: the original's progress bar steps */
  int show_progress;

  /* called with the native's id after its arguments are popped and before it runs (the state dump of the tests uses it) */
  void (*on_native)(struct gt3_vm *vm, int id, void *user);
  void *on_native_user;
  /* the trace of the native calls (the oracle's calls.txt format), when `trace` is set */
  FILE *trace;
  /* what follows `bitmap` in a traced object result (the oracle prints " 256x256 texture=10 sha256=..."); NULL: nothing */
  void (*trace_bitmap)(const struct gt3_vm *vm, const gt3_object *o, FILE *out);
  /* the same for a mesh: the oracle prints " verts=954 edges=... sha256=... topo=... xyz=... w=..." */
  void (*trace_mesh)(const struct gt3_vm *vm, const gt3_object *o, FILE *out);
  /* frees the `data` of a mesh object (a bitmap's is one block); set by gt3_register_natives */
  void (*destroy_mesh)(void *data);
  /* tells the collector which handles the `data` of an object refers to (a mesh: its materials); set by gt3_register_natives */
  void (*walk_data)(const struct gt3_object *o, void (*visit)(void *ctx, int32_t handle), void *ctx);
  /* called before an object's data is freed: the host frees what the data points to (the data itself is freed by the machine) */
  void (*release_data)(int32_t cls, void *data);
  uint32_t *free_slot; /* handles of freed objects, reused first */
  uint32_t n_free, cap_free, n_collected;
  const char *phase;
  int trace_seq, trace_next_serial;
  int32_t *trace_handle; /* trace_handle[serial - 1] is the handle of the object that has that serial */
  uint32_t trace_cap;

  void *user; /* for the natives */
} gt3_vm;

GT3_CSL_API int gt3_vm_init(gt3_vm *vm, const gt3_program *program);
GT3_CSL_API void gt3_vm_exit(gt3_vm *vm);
/* The original's sBroker: the objects the script can no longer reach are freed. Called between two executions of the script (nothing is on the stacks); the roots are the
   globals (every value that is the handle of an object counts: a small integer that looks like one keeps an object alive, never frees one that is in use), the objects
   they refer to through links, children and data. Returns the number of objects freed; does nothing while the machine traces, because the trace numbers the objects. */
GT3_CSL_API uint32_t gt3_vm_collect(gt3_vm *vm);
/* the number of objects that exist */
GT3_CSL_API uint32_t gt3_vm_live(const gt3_vm *vm);
GT3_CSL_API void gt3_vm_add_native(gt3_vm *vm, int id, int ints, int objs, int ret, gt3_native_fn fn, const char *name);

/* Runs user word `word`; 1 when it ended without an error. The globals survive, the stacks do not. */
GT3_CSL_API int gt3_vm_execute(gt3_vm *vm, int word);

GT3_CSL_API int32_t gt3_global(const gt3_vm *vm, int nr);
GT3_CSL_API void gt3_set_global(gt3_vm *vm, int nr, int32_t value);

GT3_CSL_API void gt3_push_i(gt3_vm *vm, int32_t v);

/* objects: a new one is all zero but its class; a handle of 0 is the null object, and a handle that is not an object gives NULL */
GT3_CSL_API int32_t gt3_new_object(gt3_vm *vm, uint32_t cls);
GT3_CSL_API gt3_object *gt3_object_get(const gt3_vm *vm, int32_t handle);
GT3_CSL_API int gt3_object_add_child(gt3_object *o, int32_t child);

/* The text of a string pushed by the machine, or NULL when the integer is not one. */
GT3_CSL_API const char *gt3_string(const gt3_vm *vm, int32_t value);

/* 16.16 helpers shared with the natives: the exact semantics of sMulShift / sDivShift / sMulDiv / the truncating float to integer conversion of the
   original's _ftol2 */
GT3_CSL_API int32_t gt3_mul_shift(int32_t a, int32_t b);
GT3_CSL_API int32_t gt3_div_shift(int32_t a, int32_t b);
GT3_CSL_API int32_t gt3_mul_div(int32_t a, int32_t b, int32_t c);
GT3_CSL_API int32_t gt3_ftoi(float f);
GT3_CSL_API int32_t gt3_dtoi(double d);

#ifdef __cplusplus
}
#endif

#endif /* GT3_CSL_H_INCLUDED */

#ifdef GT3_CSL_IMPLEMENTATION
#ifndef GT3_CSL_IMPLEMENTATION_DONE
#define GT3_CSL_IMPLEMENTATION_DONE

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- 16.16 and conversions ---- */

GT3_CSL_API int32_t gt3_mul_shift(int32_t a, int32_t b) { return (int32_t)(uint32_t)(((int64_t)a * (int64_t)b) >> 16); }

/* idiv of (a << 16) by b: a division by zero or an overflow traps on the x86; here it gives 0 */
GT3_CSL_API int32_t gt3_div_shift(int32_t a, int32_t b)
{
  if (b == 0) return 0;
  const int64_t q = ((int64_t)a * 65536) / (int64_t)b;
  return (int32_t)q;
}

GT3_CSL_API int32_t gt3_mul_div(int32_t a, int32_t b, int32_t c)
{
  if (c == 0) return 0;
  return (int32_t)(((int64_t)a * (int64_t)b) / (int64_t)c);
}

/* a conversion that truncates, like the original's _ftol2, and gives the x86's "integer indefinite" for what does not fit */
GT3_CSL_API int32_t gt3_ftoi(float f)
{
  if (!(f > -2147483904.0f && f < 2147483648.0f)) return (int32_t)0x80000000u;
  return (int32_t)f;
}

GT3_CSL_API int32_t gt3_dtoi(double d)
{
  if (!(d > -2147483649.0 && d < 2147483648.0)) return (int32_t)0x80000000u;
  return (int32_t)d;
}

/* ---- the program ---- */

static uint32_t gt3_le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void gt3_fail(char *error, size_t error_size, const char *what, unsigned long a)
{
  if (error_size) snprintf(error, error_size, "%s (%lu)", what, a);
}

GT3_CSL_API void gt3_program_free(gt3_program *p)
{
  free(p->words);
  free(p->strings);
  memset(p, 0, sizeof *p);
}

GT3_CSL_API int gt3_program_load(gt3_program *p, const void *pac, size_t size, char *error, size_t error_size)
{
  const uint8_t *d = (const uint8_t *)pac;
  memset(p, 0, sizeof *p);
  if (size < 28u) {
    gt3_fail(error, error_size, "script: shorter than its header", (unsigned long)size);
    return 0;
  }
  const uint32_t code_off = gt3_le32(d), func_off = gt3_le32(d + 4), imm_off = gt3_le32(d + 8), func_count = gt3_le32(d + 12), str_off = gt3_le32(d + 20),
                 var_off = gt3_le32(d + 24);
  if (code_off < 28u || code_off > imm_off || imm_off > func_off || func_off > size || (uint64_t)func_off + 8u * func_count > size || var_off > str_off ||
      str_off > size) {
    gt3_fail(error, error_size, "script: the header does not describe the streams", (unsigned long)code_off);
    return 0;
  }
  /* the streams: code bytes [code_off, imm_off), immediates [imm_off, func_off), variable indices [var_off, str_off) */
  const uint8_t *src = d + code_off, *src_end = d + imm_off;
  const uint8_t *imm = d + imm_off, *imm_end = d + func_off;
  const uint8_t *pvar = d + var_off, *pvar_end = d + str_off;
  const uint8_t *table = d + func_off;

  p->words = (uint32_t *)calloc((size_t)(imm_off - code_off) + 2u, sizeof(uint32_t));
  if (!p->words) {
    gt3_fail(error, error_size, "script: out of memory", 0);
    return 0;
  }
  uint32_t n = 0;
  p->words[n++] = 0;
  uint32_t cmd;
  do {
    if (src >= src_end) {
      gt3_fail(error, error_size, "script: the code stream ends without the end word", (unsigned long)n);
      goto fail;
    }
    cmd = (uint32_t)(*src++) << 24;
    switch (cmd >> 24) {
    case GT3_CODE:
      if (src >= src_end || src[0] >= func_count) {
        gt3_fail(error, error_size, "script: a native call outside the function table", (unsigned long)n);
        goto fail;
      }
      cmd |= gt3_le32(table + 8u * src[0]);
      src++;
      break;
    case GT3_USER:
    case GT3_FUNC:
    case GT3_STRING:
      if (src + 2 > src_end) {
        gt3_fail(error, error_size, "script: an operand beyond the code stream", (unsigned long)n);
        goto fail;
      }
      cmd |= (uint32_t)src[0] << 8;
      cmd |= src[1];
      src += 2;
      break;
    case GT3_NOP: /* an immediate comes from its own stream */
      if (imm + 4 > imm_end) {
        gt3_fail(error, error_size, "script: the immediates ran out", (unsigned long)n);
        goto fail;
      }
      cmd = gt3_le32(imm) | 0x80000000u;
      imm += 4;
      break;
    default:
      if (cmd >= 0x30000000u && cmd < 0x40000000u) { /* a variable: its index is in the variable stream */
        if (pvar + 2 > pvar_end) {
          gt3_fail(error, error_size, "script: the variable indices ran out", (unsigned long)n);
          goto fail;
        }
        cmd |= pvar[0];
        cmd |= (uint32_t)pvar[1] << 8;
        pvar += 2;
      } else if (cmd > 0x40000000u && cmd < 0x80000000u) { /* a native call with the function table index in the opcode */
        const uint32_t k = (cmd >> 24) - 0x40u;
        if (k >= func_count) {
          gt3_fail(error, error_size, "script: a native call outside the function table", (unsigned long)n);
          goto fail;
        }
        cmd = ((uint32_t)GT3_CODE << 24) | gt3_le32(table + 8u * k);
      }
      break;
    }
    p->words[n++] = cmd;
  } while (cmd != ((uint32_t)GT3_EOF << 24));
  p->count = n;

  /* the targets of the structured words: ScriptRuntime::PackedImport, the second half */
  {
    int32_t ifstack[256];
    int ifindex = 0;
    for (uint32_t i = 0; i < n; i++) {
      const uint32_t w = p->words[i];
      switch (w >> 24) {
      case GT3_FUNC: {
        const uint32_t id = w & 0xffffffu;
        if (id >= GT3_MAXUSER || ifindex != 0) {
          gt3_fail(error, error_size, "script: a function outside the table or inside a block", (unsigned long)i);
          goto fail;
        }
        p->user[id] = (int32_t)i;
        break;
      }
      case GT3_RTS:
        if (ifindex != 0) {
          gt3_fail(error, error_size, "script: a return inside a block", (unsigned long)i);
          goto fail;
        }
        break;
      case GT3_IF:
        if (ifindex >= 254) goto nest;
        ifstack[ifindex++] = (int32_t)i;
        break;
      case GT3_ELSE:
        if (ifindex < 1) goto nest;
        ifindex--;
        p->words[ifstack[ifindex]] |= i + 1;
        ifstack[ifindex++] = (int32_t)i;
        break;
      case GT3_THEN:
        if (ifindex < 1) goto nest;
        ifindex--;
        p->words[ifstack[ifindex]] |= i + 1;
        break;
      case GT3_DO:
        if (ifindex >= 254) goto nest;
        ifstack[ifindex + 0] = (int32_t)i;
        ifindex += 2;
        break;
      case GT3_WHILE:
        if (ifindex < 2) goto nest;
        ifindex -= 2;
        ifstack[ifindex + 1] = (int32_t)i;
        ifindex += 2;
        break;
      case GT3_REPEAT:
        if (ifindex < 2) goto nest;
        ifindex -= 2;
        p->words[i] |= (uint32_t)ifstack[ifindex + 0];
        p->words[ifstack[ifindex + 1]] |= i + 1;
        break;
      default: break;
      }
    }
    goto ok;
  nest:
    gt3_fail(error, error_size, "script: unbalanced blocks", 0);
    goto fail;
  }
ok:
  p->string_bytes = (uint32_t)(size - str_off);
  p->strings = (uint8_t *)malloc((size_t)p->string_bytes + 1u);
  if (!p->strings) {
    gt3_fail(error, error_size, "script: out of memory", 0);
    goto fail;
  }
  memcpy(p->strings, d + str_off, p->string_bytes);
  p->strings[p->string_bytes] = 0;
  return 1;
fail:
  gt3_program_free(p);
  return 0;
}

/* ---- objects ---- */

GT3_CSL_API int32_t gt3_new_object(gt3_vm *vm, uint32_t cls)
{
  if (vm->n_free) {
    gt3_object *o = (gt3_object *)calloc(1, sizeof *o);
    if (!o) return 0;
    o->cls = cls;
    const uint32_t slot = vm->free_slot[--vm->n_free];
    vm->object[slot] = o;
    return (int32_t)slot + 1;
  }
  if (vm->n_objects == vm->cap_objects) {
    const uint32_t cap = vm->cap_objects ? vm->cap_objects * 2u : 1024u;
    gt3_object **bigger = (gt3_object **)realloc(vm->object, cap * sizeof *bigger);
    if (!bigger) return 0;
    vm->object = bigger;
    vm->cap_objects = cap;
  }
  gt3_object *o = (gt3_object *)calloc(1, sizeof *o);
  if (!o) return 0;
  o->cls = cls;
  vm->object[vm->n_objects++] = o;
  return (int32_t)vm->n_objects;
}

GT3_CSL_API gt3_object *gt3_object_get(const gt3_vm *vm, int32_t handle)
{
  if (handle < 1 || (uint32_t)handle > vm->n_objects) return NULL;
  return vm->object[handle - 1];
}

GT3_CSL_API int gt3_object_add_child(gt3_object *o, int32_t child)
{
  if (o->n_child == o->cap_child) {
    const uint32_t cap = o->cap_child ? o->cap_child * 2u : 4u;
    int32_t *bigger = (int32_t *)realloc(o->child, cap * sizeof *bigger);
    if (!bigger) return 0;
    o->child = bigger;
    o->cap_child = cap;
  }
  o->child[o->n_child++] = child;
  return 1;
}

static void gt3_object_free(const gt3_vm *vm, gt3_object *o)
{
  if (!o) return;
  free(o->child);
  if (o->cls == GT3_CID_MESH && vm->destroy_mesh)
    vm->destroy_mesh(o->data);
  else {
    if (o->data && vm->release_data) vm->release_data(o->cls, o->data);
    free(o->data);
  }
  free(o);
}

typedef struct gt3_mark {
  uint8_t *mark;
  int32_t *stack;
  size_t n, cap;
  const gt3_vm *vm;
  int ok;
} gt3_mark;

static void gt3_mark_visit(void *ctx, int32_t h)
{
  gt3_mark *m = (gt3_mark *)ctx;
  if (h < 1 || (uint32_t)h > m->vm->n_objects || !m->vm->object[h - 1] || m->mark[h - 1]) return;
  m->mark[h - 1] = 1;
  if (m->n == m->cap) {
    const size_t cap = m->cap ? m->cap * 2 : 256;
    int32_t *bigger = (int32_t *)realloc(m->stack, cap * sizeof *bigger);
    if (!bigger) {
      m->ok = 0;
      return;
    }
    m->stack = bigger;
    m->cap = cap;
  }
  m->stack[m->n++] = h;
}

GT3_CSL_API uint32_t gt3_vm_live(const gt3_vm *vm) { return vm->n_objects - vm->n_free; }

GT3_CSL_API uint32_t gt3_vm_collect(gt3_vm *vm)
{
  if (vm->trace || vm->trace_handle || !vm->n_objects) return 0;
  gt3_mark m;
  memset(&m, 0, sizeof m);
  m.vm = vm;
  m.ok = 1;
  m.mark = (uint8_t *)calloc(vm->n_objects, 1);
  if (!m.mark) return 0;
  for (int i = 0; i < GT3_MAXGLOBAL; i++) gt3_mark_visit(&m, vm->x[i]);
  while (m.n && m.ok) {
    const gt3_object *o = vm->object[m.stack[--m.n] - 1];
    for (int i = 0; i < GT3_LINKS; i++) gt3_mark_visit(&m, o->link[i]);
    for (uint32_t i = 0; i < o->n_child; i++) gt3_mark_visit(&m, o->child[i]);
    if (vm->walk_data && o->data) vm->walk_data(o, gt3_mark_visit, &m);
  }
  uint32_t freed = 0;
  if (m.ok) {
    if (vm->n_free + vm->n_objects > vm->cap_free) {
      const uint32_t cap = vm->n_free + vm->n_objects;
      uint32_t *bigger = (uint32_t *)realloc(vm->free_slot, (size_t)cap * sizeof *bigger);
      if (bigger) {
        vm->free_slot = bigger;
        vm->cap_free = cap;
      }
    }
    for (uint32_t i = vm->n_objects; i-- > 0;) {
      if (!vm->object[i] || m.mark[i] || vm->n_free >= vm->cap_free) continue;
      gt3_object_free(vm, vm->object[i]);
      vm->object[i] = NULL;
      vm->free_slot[vm->n_free++] = i;
      freed++;
    }
  }
  vm->n_collected += freed;
  free(m.mark);
  free(m.stack);
  return freed;
}

/* ---- the machine ---- */

GT3_CSL_API int gt3_vm_init(gt3_vm *vm, const gt3_program *program)
{
  memset(vm, 0, sizeof *vm);
  vm->program = program;
  vm->x_index = GT3_MAXGLOBAL;
  vm->phase = "";
  return 1;
}

GT3_CSL_API void gt3_vm_exit(gt3_vm *vm)
{
  for (uint32_t i = 0; i < vm->n_objects; i++) gt3_object_free(vm, vm->object[i]);
  free(vm->object);
  free(vm->free_slot);
  free(vm->trace_handle);
  memset(vm, 0, sizeof *vm);
}

GT3_CSL_API void gt3_vm_add_native(gt3_vm *vm, int id, int ints, int objs, int ret, gt3_native_fn fn, const char *name)
{
  if (id < 0 || id >= GT3_MAXCODE) return;
  vm->native[id].fn = fn;
  vm->native[id].ints = (uint8_t)ints;
  vm->native[id].objs = (uint8_t)objs;
  vm->native[id].ret = (uint8_t)ret;
  vm->native[id].name = name;
}

GT3_CSL_API int32_t gt3_global(const gt3_vm *vm, int nr) { return (nr >= 0 && nr < GT3_MAXGLOBAL) ? vm->x[nr] : 0; }
GT3_CSL_API void gt3_set_global(gt3_vm *vm, int nr, int32_t value)
{
  if (nr >= 0 && nr < GT3_MAXGLOBAL) vm->x[nr] = value;
}

GT3_CSL_API const char *gt3_string(const gt3_vm *vm, int32_t value)
{
  const int64_t off = (int64_t)(uint32_t)value - (int64_t)GT3_STRING_BASE;
  if (off < 0 || off >= (int64_t)vm->program->string_bytes) return NULL;
  return (const char *)vm->program->strings + off;
}

static void gt3_push_o(gt3_vm *vm, int32_t o)
{
  if (vm->o_index < GT3_MAXSTACK)
    vm->ostack[++vm->o_index] = o;
  else
    vm->error = 1;
}

GT3_CSL_API void gt3_push_i(gt3_vm *vm, int32_t v)
{
  if (vm->i_index < GT3_MAXSTACK)
    vm->istack[++vm->i_index] = v;
  else
    vm->error = 1;
}

static int32_t gt3_pop_i(gt3_vm *vm)
{
  if (vm->i_index > 0) return vm->istack[vm->i_index--];
  vm->error = 1;
  return 0;
}

static int32_t gt3_pop_o(gt3_vm *vm)
{
  if (vm->o_index > 0) return vm->ostack[vm->o_index--];
  vm->error = 1;
  return 0;
}

/* ---- the trace of the native calls: the format of the oracle's calls.txt ---- */

static int32_t gt3_serial(gt3_vm *vm, int32_t handle)
{
  gt3_object *o = gt3_object_get(vm, handle);
  if (!o) return 0;
  if (!o->trace_serial) {
    o->trace_serial = ++vm->trace_next_serial;
    if ((uint32_t)o->trace_serial > vm->trace_cap) {
      const uint32_t cap = vm->trace_cap ? vm->trace_cap * 2u : 1024u;
      int32_t *bigger = (int32_t *)realloc(vm->trace_handle, cap * sizeof *bigger);
      if (bigger) {
        vm->trace_handle = bigger;
        vm->trace_cap = cap;
      }
    }
    if ((uint32_t)o->trace_serial <= vm->trace_cap) vm->trace_handle[o->trace_serial - 1] = handle;
  }
  return o->trace_serial;
}

static void gt3_trace_begin(gt3_vm *vm, int id, const gt3_native *n, const int32_t *objs, const int32_t *ints)
{
  fprintf(vm->trace, "C %d %s 0x%02x %s", vm->trace_seq++, vm->phase, id, n->name ? n->name : "?");
  for (int i = 0; i < n->objs; i++) fprintf(vm->trace, " o%d", gt3_serial(vm, objs[i]));
  for (int i = 0; i < n->ints; i++) {
    const char *s = gt3_string(vm, ints[i]);
    if (s)
      fprintf(vm->trace, " \"%s\"", s);
    else
      fprintf(vm->trace, " %08x", (unsigned)ints[i]);
  }
}

static void gt3_trace_end(gt3_vm *vm, const gt3_native *n, int32_t result)
{
  if (n->ret == 1) {
    fprintf(vm->trace, " -> %08x\n", (unsigned)result);
  } else if (n->ret == 2) {
    gt3_object *o = gt3_object_get(vm, result);
    const int32_t serial = gt3_serial(vm, result);
    if (!o)
      fprintf(vm->trace, " -> o%d \n", serial);
    else if (o->cls == GT3_CID_BITMAP) {
      fprintf(vm->trace, " -> o%d bitmap", serial);
      if (vm->trace_bitmap) vm->trace_bitmap(vm, o, vm->trace);
      fprintf(vm->trace, "\n");
    } else if (o->cls == GT3_CID_MESH) {
      fprintf(vm->trace, " -> o%d mesh", serial);
      if (vm->trace_mesh) vm->trace_mesh(vm, o, vm->trace);
      fprintf(vm->trace, "\n");
    } else
      fprintf(vm->trace, " -> o%d class=%08x\n", serial, (unsigned)o->cls);
  } else {
    fprintf(vm->trace, "\n");
  }
}

/* ---- the interpreter (ScriptRuntime::Interpret) ---- */

static void gt3_interpret(gt3_vm *vm)
{
  const gt3_program *prog = vm->program;
  if (vm->pc < 0 || (uint32_t)vm->pc >= prog->count) {
    vm->error = 1;
    return;
  }
  const uint32_t code = prog->words[vm->pc++];
  int32_t *ptr;
  if (code & 0x80000000u) { /* a 31-bit immediate, sign extended */
    gt3_push_i(vm, (int32_t)((code & 0x7fffffffu) | ((code & 0x40000000u) << 1)));
    return;
  }
  if (!(code & 0xff000000u)) return;
  int32_t imm = (int32_t)(code & 0x00ffffffu);
  int32_t a;

  switch (code >> 24) {
  case GT3_IF:
  case GT3_WHILE:
    if (!gt3_pop_i(vm)) vm->pc = imm;
    if ((uint32_t)imm >= prog->count) vm->error = 1; /* the original checks the operand of every word against the program size; only a jump can be wrong */
    break;
  case GT3_ELSE:
  case GT3_REPEAT:
    vm->pc = imm;
    if ((uint32_t)imm >= prog->count) vm->error = 1;
    break;
  case GT3_THEN:
  case GT3_DO: break;

  case GT3_CODE:
    if (imm < GT3_MAXCODE) {
      const gt3_native *n = &vm->native[imm];
      if (n->fn) {
        int32_t objs[32], ints[32];
        if (vm->show_progress) vm->progress++;
        for (int i = n->objs; i > 0;) {
          i--;
          objs[i] = gt3_pop_o(vm);
        }
        for (int i = n->ints; i > 0;) {
          i--;
          ints[i] = gt3_pop_i(vm);
        }
        if (vm->trace) gt3_trace_begin(vm, imm, n, objs, ints);
        if (vm->on_native) vm->on_native(vm, (int)imm, vm->on_native_user);
        const int32_t r = n->fn(vm, objs, ints);
        if (vm->trace) gt3_trace_end(vm, n, r);
        if (n->ret == 1) gt3_push_i(vm, r);
        if (n->ret == 2) gt3_push_o(vm, r);
      }
    } else
      vm->error = 1;
    break;
  case GT3_USER:
    if (imm < GT3_MAXUSER && vm->program->user[imm] && vm->r_index < GT3_MAXCALLS) {
      vm->rstack[vm->r_index++] = vm->pc;
      vm->x_index += GT3_MAXLOCAL;
      vm->pc = vm->program->user[imm];
    }
    break;
  case GT3_FUNC: break;
  case GT3_STRING: gt3_push_i(vm, (int32_t)(GT3_STRING_BASE + (uint32_t)imm)); break;

  case GT3_STORELOCI:
  case GT3_STORELOCO:
  case GT3_FETCHLOCI:
  case GT3_FETCHLOCO:
    if (imm >= GT3_MAXLOCAL || vm->x_index + imm >= GT3_MAXGLOBAL + GT3_MAXLOCAL * GT3_MAXCALLS) {
      vm->error = 1;
      return;
    }
    ptr = &vm->x[imm + vm->x_index];
    goto fetchstore;
  case GT3_STOREGLOI:
  case GT3_STOREGLOO:
  case GT3_FETCHGLOI:
  case GT3_FETCHGLOO:
    if (imm >= GT3_MAXGLOBAL) {
      vm->error = 1;
      return;
    }
    ptr = &vm->x[imm];
    goto fetchstore;

  case GT3_STOREMEMO:
  case GT3_FETCHMEMO: vm->error = 1; break;

  case GT3_STOREMEMI:
  case GT3_FETCHMEMI: {
    gt3_object *o = gt3_object_get(vm, gt3_pop_o(vm));
    if (!o || imm >= GT3_MEMBERS) {
      vm->error = 1;
      break;
    }
    ptr = &o->m[imm];
    vm->member_stores += ((code >> 24) & 3) == 0;
  }
  fetchstore:
    switch ((code >> 24) & 3) {
    case 0: *ptr = gt3_pop_i(vm); break;
    case 1: *ptr = gt3_pop_o(vm); break;
    case 2: gt3_push_i(vm, *ptr); break;
    case 3: gt3_push_o(vm, *ptr); break;
    }
    break;

  case GT3_NOP: break;
  case GT3_RTS:
    if (vm->r_index > 0) {
      vm->x_index -= GT3_MAXLOCAL;
      vm->pc = vm->rstack[--vm->r_index];
    } else
      vm->error = 1;
    break;
  case GT3_I31: vm->istack[vm->i_index] ^= (int32_t)0x80000000u; break;
#define GT3_BINARY(expr)            \
  a = gt3_pop_i(vm);                \
  vm->istack[vm->i_index] = (expr); \
  break
  case GT3_ADD: GT3_BINARY((int32_t)((uint32_t)vm->istack[vm->i_index] + (uint32_t)a));
  case GT3_SUB: GT3_BINARY((int32_t)((uint32_t)vm->istack[vm->i_index] - (uint32_t)a));
  case GT3_MUL: GT3_BINARY(gt3_mul_shift(vm->istack[vm->i_index], a));
  case GT3_DIV: GT3_BINARY(gt3_div_shift(vm->istack[vm->i_index], a));
  case GT3_MOD: GT3_BINARY(a == 0 || (a == -1) ? 0 : vm->istack[vm->i_index] % a);
  case GT3_MIN: GT3_BINARY(vm->istack[vm->i_index] < a ? vm->istack[vm->i_index] : a);
  case GT3_MAX: GT3_BINARY(vm->istack[vm->i_index] > a ? vm->istack[vm->i_index] : a);
  case GT3_AND: GT3_BINARY(vm->istack[vm->i_index] & a);
  case GT3_OR: GT3_BINARY(vm->istack[vm->i_index] | a);
  case GT3_GE: GT3_BINARY(vm->istack[vm->i_index] >= a);
  case GT3_LE: GT3_BINARY(vm->istack[vm->i_index] <= a);
  case GT3_GT: GT3_BINARY(vm->istack[vm->i_index] > a);
  case GT3_LT: GT3_BINARY(vm->istack[vm->i_index] < a);
  case GT3_EQ: GT3_BINARY(vm->istack[vm->i_index] == a);
  case GT3_NE: GT3_BINARY(vm->istack[vm->i_index] != a);
#undef GT3_BINARY
  case GT3_SIN:
    vm->istack[vm->i_index] = gt3_dtoi(sin((double)((float)vm->istack[vm->i_index] * 6.28318530717958647692528676655901f / 65536.0f)) * 65536.0);
    break;
  case GT3_COS:
    vm->istack[vm->i_index] = gt3_dtoi(cos((double)((float)vm->istack[vm->i_index] * 6.28318530717958647692528676655901f / 65536.0f)) * 65536.0);
    break;
  case GT3_DUP:
    if (vm->i_index >= GT3_MAXSTACK) {
      vm->error = 1;
      break;
    }
    vm->istack[vm->i_index + 1] = vm->istack[vm->i_index];
    vm->i_index++;
    break;
  case GT3_F2I: {
    float f;
    memcpy(&f, &vm->istack[vm->i_index], sizeof f);
    vm->istack[vm->i_index] = gt3_ftoi(f * 65536.0f);
    break;
  }
  case GT3_I2F: {
    const float f = (float)vm->istack[vm->i_index] / 65536.0f;
    memcpy(&vm->istack[vm->i_index], &f, sizeof f);
    break;
  }
  default: break;
  }
}

GT3_CSL_API int gt3_vm_execute(gt3_vm *vm, int word)
{
  vm->x_index = GT3_MAXGLOBAL;
  vm->i_index = 0;
  vm->o_index = 0;
  vm->r_index = 0;
  vm->error = 0;
  vm->error_message = "no error";
  if (word < 0 || word >= GT3_MAXUSER) return 0;
  vm->pc = vm->program->user[word];
  vm->rstack[vm->r_index++] = 0;
  while (vm->pc && !vm->error) gt3_interpret(vm);
  return !vm->error;
}

#endif /* GT3_CSL_IMPLEMENTATION_DONE */
#endif /* GT3_CSL_IMPLEMENTATION */
