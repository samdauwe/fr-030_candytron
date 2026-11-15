/* Checks for csl.h: the loader of the packed script and the interpreter, on scripts assembled here.
 *
 * The packed format (cslrt.cpp, PackedImport): a 28-byte header of seven offsets and counts, one byte per word in the code stream, with the operands
 * of CODE / USER / FUNC / STRING in the same stream (the function table index, or two bytes big endian), the 31-bit immediates in their own stream
 * (the byte 0 means "the next immediate"), the variable indices of the words 0x30..0x3f as two bytes little endian in a third stream, the
 * function table of 8-byte entries (the native id first), and the strings.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "support/check.h"
#include "support/suite.h"

#define GT3_CSL_STATIC
#define GT3_CSL_IMPLEMENTATION
#include <candytron/core/csl.h>

typedef struct pac {
  uint8_t code[4096];
  size_t n_code;
  uint32_t imm[512];
  size_t n_imm;
  uint8_t vars[512];
  size_t n_vars;
  uint32_t table[16];
  size_t n_table;
  const char *strings;
} pac;

static void emit(pac *p, int byte) { p->code[p->n_code++] = (uint8_t)byte; }
/* an immediate: the byte 0 and the 31-bit value (two's complement, the VM sign-extends bit 30) */
static void imm(pac *p, int32_t v)
{
  emit(p, 0);
  p->imm[p->n_imm++] = (uint32_t)v & 0x7fffffffu;
}
static void op(pac *p, int word) { emit(p, word); }
static void func(pac *p, int id)
{
  emit(p, GT3_FUNC);
  emit(p, id >> 8);
  emit(p, id & 255);
}
static void var(pac *p, int word, int index)
{
  emit(p, word);
  p->vars[p->n_vars++] = (uint8_t)(index & 255);
  p->vars[p->n_vars++] = (uint8_t)(index >> 8);
}
static void user(pac *p, int id)
{
  emit(p, GT3_USER);
  emit(p, id >> 8);
  emit(p, id & 255);
}
static void str(pac *p, int offset)
{
  emit(p, GT3_STRING);
  emit(p, offset >> 8);
  emit(p, offset & 255);
}
/* a native call through the function table: native id `id` is table entry `n` */
static void call(pac *p, int id)
{
  size_t n;
  for (n = 0; n < p->n_table; n++)
    if (p->table[n] == (uint32_t)id) break;
  if (n == p->n_table) p->table[p->n_table++] = (uint32_t)id;
  emit(p, GT3_CODE);
  emit(p, (int)n);
}

static void put32(uint8_t *d, uint32_t v)
{
  d[0] = (uint8_t)v;
  d[1] = (uint8_t)(v >> 8);
  d[2] = (uint8_t)(v >> 16);
  d[3] = (uint8_t)(v >> 24);
}

/* the bytes of the packed file; the caller frees */
static uint8_t *build(pac *p, size_t *size)
{
  emit(p, GT3_EOF);
  const size_t strings = p->strings ? strlen(p->strings) + 1 : 1;
  const uint32_t code_off = 28, imm_off = (uint32_t)(code_off + p->n_code), func_off = (uint32_t)(imm_off + 4 * p->n_imm),
                 var_off = (uint32_t)(func_off + 8 * p->n_table), str_off = (uint32_t)(var_off + p->n_vars);
  *size = str_off + strings;
  uint8_t *d = (uint8_t *)calloc(1, *size);
  put32(d + 0, code_off);
  put32(d + 4, func_off);
  put32(d + 8, imm_off);
  put32(d + 12, (uint32_t)p->n_table);
  put32(d + 16, var_off);
  put32(d + 20, str_off);
  put32(d + 24, var_off);
  memcpy(d + code_off, p->code, p->n_code);
  for (size_t i = 0; i < p->n_imm; i++) put32(d + imm_off + 4 * i, p->imm[i]);
  for (size_t i = 0; i < p->n_table; i++) put32(d + func_off + 8 * i, p->table[i]);
  memcpy(d + var_off, p->vars, p->n_vars);
  if (p->strings) memcpy(d + str_off, p->strings, strlen(p->strings));
  return d;
}

static int run(pac *p, gt3_program *prog, gt3_vm *vm, int word)
{
  size_t size;
  uint8_t *d = build(p, &size);
  char error[256];
  const int ok = gt3_program_load(prog, d, size, error, sizeof error);
  free(d);
  CHECK(ok, "load: %s", error);
  if (!ok) return 0;
  gt3_vm_init(vm, prog);
  return gt3_vm_execute(vm, word);
}

static int32_t g_seen[8];
static int g_n_seen;
static int32_t record(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  (void)vm;
  g_seen[0] = objs[0];
  g_seen[1] = ints[0];
  g_seen[2] = ints[1];
  g_n_seen++;
  return ints[0] * 10 + ints[1];
}
static int32_t make_object(gt3_vm *vm, const int32_t *objs, const int32_t *ints)
{
  (void)objs;
  (void)ints;
  return gt3_new_object(vm, GT3_CID_SCENE);
}

int suite_csl_vm(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  gt3_program prog;
  gt3_vm vm;

  /* arithmetic in 16.16: 3.5 * 2 = 7, 7 / 2 = 3.5, the 31-bit immediates, signs */
  {
    pac p;
    memset(&p, 0, sizeof p);
    func(&p, 1);
    imm(&p, 0x38000);          /* 3.5 */
    imm(&p, 0x20000);          /* 2 */
    op(&p, GT3_MUL);
    var(&p, GT3_STOREGLOI, 0); /* g0 = 7.0 */
    imm(&p, 0x70000);
    imm(&p, 0x20000);
    op(&p, GT3_DIV);
    var(&p, GT3_STOREGLOI, 1); /* g1 = 3.5 */
    imm(&p, -5);               /* a negative immediate */
    var(&p, GT3_STOREGLOI, 2);
    imm(&p, 10);
    imm(&p, 3);
    op(&p, GT3_MOD);
    var(&p, GT3_STOREGLOI, 3);
    imm(&p, 7);
    imm(&p, 9);
    op(&p, GT3_MIN);
    var(&p, GT3_STOREGLOI, 4);
    imm(&p, 7);
    imm(&p, 9);
    op(&p, GT3_MAX);
    var(&p, GT3_STOREGLOI, 5);
    imm(&p, 6);
    imm(&p, 3);
    op(&p, GT3_AND);
    imm(&p, 8);
    op(&p, GT3_OR);
    var(&p, GT3_STOREGLOI, 6);
    imm(&p, 5);
    op(&p, GT3_I31); /* flips the top bit */
    var(&p, GT3_STOREGLOI, 7);
    op(&p, GT3_RTS);
    CHECK(run(&p, &prog, &vm, 1), "arithmetic runs");
    CHECK(gt3_global(&vm, 0) == 0x70000 && gt3_global(&vm, 1) == 0x38000, "16.16 multiply and divide: %x %x", (unsigned)gt3_global(&vm, 0),
          (unsigned)gt3_global(&vm, 1));
    CHECK(gt3_global(&vm, 2) == -5, "a negative immediate: %d", (int)gt3_global(&vm, 2));
    CHECK(gt3_global(&vm, 3) == 1 && gt3_global(&vm, 4) == 7 && gt3_global(&vm, 5) == 9 && gt3_global(&vm, 6) == (2 | 8), "mod, min, max, and/or: %d %d %d %d",
          (int)gt3_global(&vm, 3), (int)gt3_global(&vm, 4), (int)gt3_global(&vm, 5), (int)gt3_global(&vm, 6));
    CHECK((uint32_t)gt3_global(&vm, 7) == 0x80000005u, "I31: %x", (unsigned)gt3_global(&vm, 7));
    gt3_vm_exit(&vm);
    gt3_program_free(&prog);
  }

  /* the helpers are those of the original's asm: sMulShift, sDivShift, sMulDiv */
  CHECK(gt3_mul_shift(0x7fffffff, 0x7fffffff) == (int32_t)(((int64_t)0x7fffffff * 0x7fffffff) >> 16), "mul shift keeps the low 32 bits of the shifted product");
  CHECK(gt3_mul_shift(-0x18000, 0x20000) == -0x30000, "mul shift of a negative");
  CHECK(gt3_div_shift(1, 3) == 21845 && gt3_div_shift(-1, 3) == -21845, "div shift truncates toward zero: %d %d", (int)gt3_div_shift(1, 3),
        (int)gt3_div_shift(-1, 3));
  CHECK(gt3_mul_div(60 * 44100, 0x10000, 132 * 0x10000) == 20045, "samples per beat at 132 bpm: %d", (int)gt3_mul_div(60 * 44100, 0x10000, 132 * 0x10000));
  CHECK(gt3_ftoi(2.9f) == 2 && gt3_ftoi(-2.9f) == -2 && gt3_ftoi(3.0e10f) == (int32_t)0x80000000u,
        "the float to integer conversion truncates and has an integer indefinite");

  /* floats travel as bits: 1.5 -> I2F -> 1.5f, F2I back */
  {
    pac p;
    memset(&p, 0, sizeof p);
    func(&p, 1);
    imm(&p, 0x18000);
    op(&p, GT3_I2F);
    var(&p, GT3_STOREGLOI, 0);
    imm(&p, 0x18000);
    op(&p, GT3_I2F);
    op(&p, GT3_F2I);
    var(&p, GT3_STOREGLOI, 1);
    op(&p, GT3_RTS);
    CHECK(run(&p, &prog, &vm, 1), "float words run");
    CHECK((uint32_t)gt3_global(&vm, 0) == 0x3fc00000u && gt3_global(&vm, 1) == 0x18000, "I2F %x F2I %x", (unsigned)gt3_global(&vm, 0),
          (unsigned)gt3_global(&vm, 1));
    gt3_vm_exit(&vm);
    gt3_program_free(&prog);
  }

  /* structured words: if/else/then and do/while/repeat (a loop that sums 1..4) */
  {
    pac p;
    memset(&p, 0, sizeof p);
    func(&p, 1);
    imm(&p, 0);
    var(&p, GT3_STOREGLOI, 0); /* sum */
    imm(&p, 4);
    var(&p, GT3_STOREGLOI, 1); /* i */
    op(&p, GT3_DO);
    var(&p, GT3_FETCHGLOI, 1);
    imm(&p, 0);
    op(&p, GT3_GT);
    op(&p, GT3_WHILE);
    var(&p, GT3_FETCHGLOI, 0);
    var(&p, GT3_FETCHGLOI, 1);
    op(&p, GT3_ADD);
    var(&p, GT3_STOREGLOI, 0);
    var(&p, GT3_FETCHGLOI, 1);
    imm(&p, 1);
    op(&p, GT3_SUB);
    var(&p, GT3_STOREGLOI, 1);
    op(&p, GT3_REPEAT);
    imm(&p, 1);
    op(&p, GT3_IF);
    imm(&p, 11);
    var(&p, GT3_STOREGLOI, 2);
    op(&p, GT3_ELSE);
    imm(&p, 22);
    var(&p, GT3_STOREGLOI, 2);
    op(&p, GT3_THEN);
    imm(&p, 0);
    op(&p, GT3_IF);
    imm(&p, 33);
    var(&p, GT3_STOREGLOI, 3);
    op(&p, GT3_ELSE);
    imm(&p, 44);
    var(&p, GT3_STOREGLOI, 3);
    op(&p, GT3_THEN);
    op(&p, GT3_RTS);
    CHECK(run(&p, &prog, &vm, 1), "control flow runs");
    CHECK(gt3_global(&vm, 0) == 10 && gt3_global(&vm, 1) == 0, "loop: sum %d i %d", (int)gt3_global(&vm, 0), (int)gt3_global(&vm, 1));
    CHECK(gt3_global(&vm, 2) == 11 && gt3_global(&vm, 3) == 44, "if: %d %d", (int)gt3_global(&vm, 2), (int)gt3_global(&vm, 3));
    gt3_vm_exit(&vm);
    gt3_program_free(&prog);
  }

  /* calls: a user word with its own locals, DUP, and a native that takes an object and two integers */
  {
    pac p;
    memset(&p, 0, sizeof p);
    p.strings = "hello";
    func(&p, 1);
    imm(&p, 100);
    var(&p, GT3_STORELOCI, 0); /* local 0 of word 1 */
    user(&p, 2);
    var(&p, GT3_FETCHLOCI, 0); /* still 100: word 2 has its own locals */
    var(&p, GT3_STOREGLOI, 0);
    imm(&p, 7);
    op(&p, GT3_DUP);
    op(&p, GT3_ADD);
    var(&p, GT3_STOREGLOI, 1); /* 14 */
    call(&p, 0x30);
    var(&p, GT3_STOREGLOO, 2); /* a new object */
    var(&p, GT3_FETCHGLOO, 2);
    imm(&p, 4);
    imm(&p, 5);
    call(&p, 0x31); /* record(obj, 4, 5) -> 45 */
    var(&p, GT3_STOREGLOI, 3);
    str(&p, 0);
    var(&p, GT3_STOREGLOI, 4);
    op(&p, GT3_RTS);
    func(&p, 2);
    imm(&p, 5);
    var(&p, GT3_STORELOCI, 0);
    op(&p, GT3_RTS);
    size_t size;
    uint8_t *d = build(&p, &size);
    char error[256];
    CHECK(gt3_program_load(&prog, d, size, error, sizeof error), "load: %s", error);
    free(d);
    gt3_vm_init(&vm, &prog);
    gt3_vm_add_native(&vm, 0x30, 0, 0, 2, make_object, "make");
    gt3_vm_add_native(&vm, 0x31, 2, 1, 1, record, "record");
    g_n_seen = 0;
    CHECK(gt3_vm_execute(&vm, 1), "calls run: error %d", vm.error);
    CHECK(gt3_global(&vm, 0) == 100 && gt3_global(&vm, 1) == 14, "locals are per call, DUP: %d %d", (int)gt3_global(&vm, 0), (int)gt3_global(&vm, 1));
    CHECK(g_n_seen == 1 && g_seen[0] == gt3_global(&vm, 2) && g_seen[0] == 1 && g_seen[1] == 4 && g_seen[2] == 5,
          "the native sees the object and its integers in order: %d %d %d", (int)g_seen[0], (int)g_seen[1], (int)g_seen[2]);
    CHECK(gt3_global(&vm, 3) == 45, "the native's result: %d", (int)gt3_global(&vm, 3));
    CHECK(gt3_string(&vm, gt3_global(&vm, 4)) && !strcmp(gt3_string(&vm, gt3_global(&vm, 4)), "hello") && !gt3_string(&vm, 5),
          "a string is the address of its text");
    gt3_vm_exit(&vm);
    gt3_program_free(&prog);
  }

  /* members: STOREMEMI and FETCHMEMI go to the slots of the object on the object stack; a native that is not registered takes nothing from the stacks */
  {
    pac p;
    memset(&p, 0, sizeof p);
    func(&p, 1);
    call(&p, 0x30);
    var(&p, GT3_STOREGLOO, 0);
    imm(&p, 0x12345);
    var(&p, GT3_FETCHGLOO, 0);
    var(&p, GT3_STOREMEMI, 3); /* obj.m[3] = 0x12345 */
    var(&p, GT3_FETCHGLOO, 0);
    var(&p, GT3_FETCHMEMI, 3);
    var(&p, GT3_STOREGLOI, 1);
    imm(&p, 99);
    call(&p, 0x77); /* not registered: nothing is popped, nothing is called */
    var(&p, GT3_STOREGLOI, 2);
    op(&p, GT3_RTS);
    size_t size;
    uint8_t *d = build(&p, &size);
    char error[256];
    CHECK(gt3_program_load(&prog, d, size, error, sizeof error), "load: %s", error);
    free(d);
    gt3_vm_init(&vm, &prog);
    gt3_vm_add_native(&vm, 0x30, 0, 0, 2, make_object, "make");
    CHECK(gt3_vm_execute(&vm, 1), "members run");
    CHECK(gt3_global(&vm, 1) == 0x12345 && gt3_object_get(&vm, 1)->m[3] == 0x12345, "a member round trip: %x", (unsigned)gt3_global(&vm, 1));
    CHECK(gt3_global(&vm, 2) == 99, "an unregistered native leaves its arguments alone: %d", (int)gt3_global(&vm, 2));
    gt3_vm_exit(&vm);
    gt3_program_free(&prog);
  }

  /* errors: a fetch from an empty stack, a member of the null object, a script that is cut short */
  {
    pac p;
    memset(&p, 0, sizeof p);
    func(&p, 1);
    var(&p, GT3_FETCHGLOO, 0); /* the null object */
    var(&p, GT3_FETCHMEMI, 0);
    op(&p, GT3_RTS);
    CHECK(!run(&p, &prog, &vm, 1) && vm.error, "a member of the null object is an error");
    gt3_vm_exit(&vm);
    gt3_program_free(&prog);

    memset(&p, 0, sizeof p);
    func(&p, 1);
    var(&p, GT3_STOREGLOI, 0); /* nothing on the stack */
    op(&p, GT3_RTS);
    CHECK(!run(&p, &prog, &vm, 1), "popping an empty stack is an error");
    gt3_vm_exit(&vm);
    gt3_program_free(&prog);

    memset(&p, 0, sizeof p);
    func(&p, 1);
    for (int i = 0; i < 300; i++) imm(&p, i);
    op(&p, GT3_RTS);
    CHECK(!run(&p, &prog, &vm, 1), "a stack of more than 256 is an error");
    gt3_vm_exit(&vm);
    gt3_program_free(&prog);

    char error[256];
    uint8_t junk[40] = { 0 };
    CHECK(!gt3_program_load(&prog, junk, 10, error, sizeof error) && strstr(error, "header"), "a file shorter than the header: %s", error);
    memset(&p, 0, sizeof p);
    func(&p, 1);
    op(&p, GT3_RTS);
    size_t size;
    uint8_t *d = build(&p, &size);
    CHECK(gt3_program_load(&prog, d, size, error, sizeof error), "a good script loads");
    gt3_program_free(&prog);
    put32(d + 4, 0xfffffff0u);             /* the function table beyond the file */
    CHECK(!gt3_program_load(&prog, d, size, error, sizeof error), "a table beyond the file is refused");
    put32(d + 4, 28 + (uint32_t)p.n_code); /* the table where it was, but a code stream that ends without the end word */
    d[28 + p.n_code - 1] = GT3_NOP + 0x24; /* the last word is an IF instead of EOF: unbalanced, and no end */
    CHECK(!gt3_program_load(&prog, d, size, error, sizeof error), "a code stream without the end word is refused: %s", error);
    free(d);
  }

  /* the collector: what a global refers to stays (through links and children), the rest goes, handles are reused */
  {
    pac p;
    memset(&p, 0, sizeof p);
    func(&p, 1);
    op(&p, GT3_RTS);
    CHECK(run(&p, &prog, &vm, 1), "the empty program runs");
    const int32_t root = gt3_new_object(&vm, GT3_CID_SCENE), kid = gt3_new_object(&vm, GT3_CID_SCENE), grand = gt3_new_object(&vm, GT3_CID_SCENE);
    const int32_t linked = gt3_new_object(&vm, GT3_CID_FXCHAIN), orphan = gt3_new_object(&vm, GT3_CID_SCENE), orphan2 = gt3_new_object(&vm, GT3_CID_SCENE);
    gt3_object_add_child(gt3_object_get(&vm, root), kid);
    gt3_object_add_child(gt3_object_get(&vm, kid), grand);
    gt3_object_get(&vm, root)->link[3] = linked;
    gt3_object_get(&vm, orphan)->link[0] = orphan2; /* a cycle among the unreachable goes too */
    gt3_object_get(&vm, orphan2)->link[0] = orphan;
    vm.x[7] = root;
    vm.x[8] = 12345; /* an integer that is no handle */
    CHECK(gt3_vm_live(&vm) == 6, "six objects");
    CHECK(gt3_vm_collect(&vm) == 2, "the two unreachable objects are freed");
    CHECK(gt3_vm_live(&vm) == 4 && gt3_object_get(&vm, orphan) == NULL && gt3_object_get(&vm, orphan2) == NULL, "four live, the orphans gone");
    CHECK(gt3_object_get(&vm, root) && gt3_object_get(&vm, kid) && gt3_object_get(&vm, grand) && gt3_object_get(&vm, linked),
          "root, child, grandchild and linked object stay");
    const int32_t again = gt3_new_object(&vm, GT3_CID_SCENE);
    CHECK(again == orphan || again == orphan2, "a freed handle is reused");
    vm.x[7] = 0;
    CHECK(gt3_vm_collect(&vm) == 5 && gt3_vm_live(&vm) == 0, "with the root gone the whole tree is freed, the new object (in no global) too: %u live",
          gt3_vm_live(&vm));
    gt3_vm_exit(&vm);
    gt3_program_free(&prog);
  }

  /* the original's limits */
  CHECK(GT3_MAXGLOBAL == 2048 && GT3_MAXLOCAL == 512 && GT3_MAXCALLS == 16 && GT3_MAXUSER == 1024 && GT3_MAXCODE == 512 && GT3_MAXSTACK == 256,
        "the limits of cslrt.hpp");

  printf("csl_vm: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures != 0;
}
