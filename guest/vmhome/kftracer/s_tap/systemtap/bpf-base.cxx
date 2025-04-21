// bpf translation pass
// Copyright (C) 2016-2022 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "bpf-internal.h"
#include "elaborate.h"
#include "session.h"
#include "staputil.h"

using namespace std;

namespace bpf {

std::ostream &
value::print(std::ostream &o) const
{
  switch (type)
    {
    case UNINIT:
      return o << "#";
    case IMM:
      return o << "$" << imm_val;
    case STR:
      return o << "$\"" << escaped_literal_string (str_val) << "\"";
    case HARDREG:
      return o << "r" << reg_val;
    case TMPREG:
      return o << "t" << reg_val;
    default:
      return o << "<BUG:unknown operand>";
    }
}

insn::insn()
  : code(-1), id(0), off(0),
    dest(NULL), src0(NULL), src1(NULL),
    prev(NULL), next(NULL)
{ }

bool
is_jmp(opcode code)
{
  if (BPF_CLASS (code) != BPF_JMP)
    return false;
  switch (BPF_OP (code))
    {
    case BPF_JA:
    case BPF_JEQ:
    case BPF_JGT:
    case BPF_JGE:
    case BPF_JSET:
    case BPF_JNE:
    case BPF_JSGT:
    case BPF_JSGE:
      return true;
    default:
      return false;
    }
}

bool
is_move(opcode c)
{
  switch (c)
    {
    case BPF_ALU64 | BPF_MOV | BPF_X:
    case BPF_ALU64 | BPF_MOV | BPF_K:
    case BPF_ALU | BPF_MOV | BPF_K:
    case BPF_LD | BPF_IMM | BPF_DW:
    case BPF_LD_MAP:
      return true;
    default:
      return false;
    }
}

bool
is_ldst(opcode c)
{
  switch (BPF_CLASS (c))
    {
    case BPF_LDX:
    case BPF_ST:
    case BPF_STX:
      return true;
    default:
      return false;
    }
}

bool
is_binary(opcode code)
{
  if (BPF_CLASS (code) != BPF_ALU64)
    return false;
  switch (BPF_OP (code))
    {
    case BPF_ADD:
    case BPF_SUB:
    case BPF_AND:
    case BPF_OR:
    case BPF_LSH:
    case BPF_RSH:
    case BPF_XOR:
    case BPF_MUL:
    case BPF_ARSH:
    case BPF_DIV:
    case BPF_MOD:
      return true;
    default:
      return false;
    }
}

bool
is_commutative(opcode code)
{
  if (BPF_CLASS (code) != BPF_ALU64)
    return false;
  switch (BPF_OP (code))
    {
    case BPF_ADD:
    case BPF_AND:
    case BPF_OR:
    case BPF_XOR:
    case BPF_MUL:
      return true;
    default:
      return false;
    }
}

/* PR29307: BPF opcode lookup for the embedded-code assembler: */

std::map<opcode, const char *> bpf_opcode_name_map;
std::map<std::string, opcode> bpf_src_opcode_map; // when operation takes SRC
std::map<std::string, opcode> bpf_imm_opcode_map; // when operation takes IMM
std::map<opcode, unsigned> bpf_opcode_category_map;

// XXX: Follows https://github.com/iovisor/bpf-docs/blob/master/eBPF.md rather than
// kernel linux/bpf_exp.y to avoid getting into weird addressing-mode syntax.
// Perhaps later, expanding the above bpf_{src,imm}_opcode_map scheme.
//
// Define as FN_{SRC,IMM}(op_name, raw_opcode, opcode, category)
// (raw_opcode is the hex opcode taken from the iovisor cheatsheet,
//  opcode is the opcode as constructed from linux bpf.h/bpf_common.h macros
//  following the scheme in linux/filter.h (yet another assembler format!)
//  These codes should be equal, both are included to sanity-check the table.)
// with FN_IMM used only for variants of SRC opcodes that take an IMM value.
//
// XXX: Does not have to be complete, just complete enough for the needs of the tapsets.
// Will gradually add opcodes over the following patches.
#ifndef __BPF_OPCODE_MAPPER
#define __BPF_OPCODE_MAPPER(FN_SRC,FN_IMM) \
  FN_SRC(add, 0x0f, BPF_ALU64 | BPF_OP(BPF_ADD) | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(add, 0x07, BPF_ALU64 | BPF_OP(BPF_ADD) | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(sub, 0x1f, BPF_ALU64 | BPF_OP(BPF_SUB) | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(sub, 0x17, BPF_ALU64 | BPF_OP(BPF_SUB) | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(mul, 0x2f, BPF_ALU64 | BPF_OP(BPF_MUL) | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(mul, 0x27, BPF_ALU64 | BPF_OP(BPF_MUL) | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(div, 0x3f, BPF_ALU64 | BPF_OP(BPF_DIV) | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(div, 0x37, BPF_ALU64 | BPF_OP(BPF_DIV) | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(or, 0x4f, BPF_ALU64 | BPF_OP(BPF_OR) | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(or, 0x47, BPF_ALU64 | BPF_OP(BPF_OR) | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(and, 0x5f, BPF_ALU64 | BPF_OP(BPF_AND) | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(and, 0x57, BPF_ALU64 | BPF_OP(BPF_AND) | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(lsh, 0x6f, BPF_ALU64 | BPF_OP(BPF_LSH) | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(lsh, 0x67, BPF_ALU64 | BPF_OP(BPF_LSH) | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(rsh, 0x7f, BPF_ALU64 | BPF_OP(BPF_RSH) | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(rsh, 0x77, BPF_ALU64 | BPF_OP(BPF_RSH) | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(neg, 0x87, BPF_ALU64 | BPF_OP(BPF_NEG) | BPF_K, BPF_ALU_ARI2), \
  FN_SRC(mod, 0x9f, BPF_ALU64 | BPF_OP(BPF_MOD) | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(mod, 0x97, BPF_ALU64 | BPF_OP(BPF_MOD) | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(xor, 0xaf, BPF_ALU64 | BPF_OP(BPF_XOR) | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(xor, 0xa7, BPF_ALU64 | BPF_OP(BPF_XOR) | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(mov, 0xbf, BPF_ALU64 | BPF_MOV | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(mov, 0xb7, BPF_ALU64 | BPF_MOV | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(arsh, 0xcf, BPF_ALU64 | BPF_OP(BPF_ARSH) | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(arsh, 0xc7, BPF_ALU64 | BPF_OP(BPF_ARSH) | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(add32, 0x0c, BPF_ALU | BPF_OP(BPF_ADD) | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(add32, 0x04, BPF_ALU | BPF_OP(BPF_ADD) | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(sub32, 0x1c, BPF_ALU | BPF_OP(BPF_SUB) | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(sub32, 0x14, BPF_ALU | BPF_OP(BPF_SUB) | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(mul32, 0x2c, BPF_ALU | BPF_OP(BPF_MUL) | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(mul32, 0x24, BPF_ALU | BPF_OP(BPF_MUL) | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(div32, 0x3c, BPF_ALU | BPF_OP(BPF_DIV) | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(div32, 0x34, BPF_ALU | BPF_OP(BPF_DIV) | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(or32, 0x4c, BPF_ALU | BPF_OP(BPF_OR) | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(or32, 0x44, BPF_ALU | BPF_OP(BPF_OR) | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(and32, 0x5c, BPF_ALU | BPF_OP(BPF_AND) | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(and32, 0x54, BPF_ALU | BPF_OP(BPF_AND) | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(lsh32, 0x6c, BPF_ALU | BPF_OP(BPF_LSH) | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(lsh32, 0x64, BPF_ALU | BPF_OP(BPF_LSH) | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(rsh32, 0x7c, BPF_ALU | BPF_OP(BPF_RSH) | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(rsh32, 0x74, BPF_ALU | BPF_OP(BPF_RSH) | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(neg32, 0x84, BPF_ALU | BPF_OP(BPF_NEG) | BPF_K, BPF_ALU_ARI2), \
  FN_SRC(mod32, 0x9c, BPF_ALU | BPF_OP(BPF_MOD) | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(mod32, 0x94, BPF_ALU | BPF_OP(BPF_MOD) | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(xor32, 0xac, BPF_ALU | BPF_OP(BPF_XOR) | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(xor32, 0xa4, BPF_ALU | BPF_OP(BPF_XOR) | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(mov32, 0xbc, BPF_ALU | BPF_MOV | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(mov32, 0xb4, BPF_ALU | BPF_MOV | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(arsh32, 0xcc, BPF_ALU | BPF_OP(BPF_ARSH) | BPF_X, BPF_ALU_ARI3), \
  FN_IMM(arsh32, 0xc4, BPF_ALU | BPF_OP(BPF_ARSH) | BPF_K, BPF_ALU_ARI3), \
  FN_SRC(lddw, 0x18, BPF_LD | BPF_DW | BPF_IMM, BPF_MEMORY_ARI3), \
  FN_SRC(ldxw, 0x61, BPF_LDX | BPF_SIZE(BPF_W) | BPF_MEM, BPF_MEMORY_ARI34_SRCOFF), \
  FN_SRC(ldxh, 0x69, BPF_LDX | BPF_SIZE(BPF_H) | BPF_MEM, BPF_MEMORY_ARI34_SRCOFF), \
  FN_SRC(ldxb, 0x71, BPF_LDX | BPF_SIZE(BPF_B) | BPF_MEM, BPF_MEMORY_ARI34_SRCOFF), \
  FN_SRC(ldxdw, 0x79, BPF_LDX | BPF_SIZE(BPF_DW) | BPF_MEM, BPF_MEMORY_ARI34_SRCOFF), \
  FN_SRC(stw, 0x62, BPF_ST | BPF_SIZE(BPF_W) | BPF_MEM, BPF_MEMORY_ARI34_DSTOFF_IMM), \
  FN_SRC(sth, 0x6a, BPF_ST | BPF_SIZE(BPF_H) | BPF_MEM, BPF_MEMORY_ARI34_DSTOFF_IMM), \
  FN_SRC(stb, 0x72, BPF_ST | BPF_SIZE(BPF_B) | BPF_MEM, BPF_MEMORY_ARI34_DSTOFF_IMM), \
  FN_SRC(stdw, 0x7a, BPF_ST | BPF_SIZE(BPF_DW) | BPF_MEM, BPF_MEMORY_ARI34_DSTOFF_IMM), \
  FN_SRC(stxw, 0x63, BPF_STX | BPF_SIZE(BPF_W) | BPF_MEM, BPF_MEMORY_ARI34_DSTOFF), \
  FN_SRC(stxh, 0x6b, BPF_STX | BPF_SIZE(BPF_H) | BPF_MEM, BPF_MEMORY_ARI34_DSTOFF), \
  FN_SRC(stxb, 0x73, BPF_STX | BPF_SIZE(BPF_B) | BPF_MEM, BPF_MEMORY_ARI34_DSTOFF), \
  FN_SRC(stxdw, 0x7b, BPF_STX | BPF_SIZE(BPF_DW) | BPF_MEM, BPF_MEMORY_ARI34_DSTOFF), \
  FN_SRC(ja, 0x05, BPF_JMP | BPF_JA, BPF_BRANCH_ARI2), \
  FN_SRC(jeq, 0x1d, BPF_JMP | BPF_OP(BPF_JEQ) | BPF_X, BPF_BRANCH_ARI4), \
  FN_IMM(jeq, 0x15, BPF_JMP | BPF_OP(BPF_JEQ) | BPF_K, BPF_BRANCH_ARI4), \
  FN_SRC(jgt, 0x2d, BPF_JMP | BPF_OP(BPF_JGT) | BPF_X, BPF_BRANCH_ARI4), \
  FN_IMM(jgt, 0x25, BPF_JMP | BPF_OP(BPF_JGT) | BPF_K, BPF_BRANCH_ARI4), \
  FN_SRC(jge, 0x3d, BPF_JMP | BPF_OP(BPF_JGE) | BPF_X, BPF_BRANCH_ARI4), \
  FN_IMM(jge, 0x35, BPF_JMP | BPF_OP(BPF_JGE) | BPF_K, BPF_BRANCH_ARI4), \
  FN_SRC(jlt, 0xad, BPF_JMP | BPF_OP(BPF_JLT) | BPF_X, BPF_BRANCH_ARI4), \
  FN_IMM(jlt, 0xa5, BPF_JMP | BPF_OP(BPF_JLT) | BPF_K, BPF_BRANCH_ARI4), \
  FN_SRC(jle, 0xbd, BPF_JMP | BPF_OP(BPF_JLE) | BPF_X, BPF_BRANCH_ARI4), \
  FN_IMM(jle, 0xb5, BPF_JMP | BPF_OP(BPF_JLE) | BPF_K, BPF_BRANCH_ARI4), \
  FN_SRC(jset, 0x4d, BPF_JMP | BPF_OP(BPF_JSET) | BPF_X, BPF_BRANCH_ARI4), \
  FN_IMM(jset, 0x45, BPF_JMP | BPF_OP(BPF_JSET) | BPF_K, BPF_BRANCH_ARI4), \
  FN_SRC(jne, 0x5d, BPF_JMP | BPF_OP(BPF_JNE) | BPF_X, BPF_BRANCH_ARI4), \
  FN_IMM(jne, 0x55, BPF_JMP | BPF_OP(BPF_JNE) | BPF_K, BPF_BRANCH_ARI4), \
  FN_SRC(jsgt, 0x6d, BPF_JMP | BPF_OP(BPF_JSGT) | BPF_X, BPF_BRANCH_ARI4), \
  FN_IMM(jsgt, 0x65, BPF_JMP | BPF_OP(BPF_JSGT) | BPF_K, BPF_BRANCH_ARI4), \
  FN_SRC(jsge, 0x7d, BPF_JMP | BPF_OP(BPF_JSGE) | BPF_X, BPF_BRANCH_ARI4), \
  FN_IMM(jsge, 0x75, BPF_JMP | BPF_OP(BPF_JSGE) | BPF_K, BPF_BRANCH_ARI4), \
  FN_SRC(jslt, 0xcd, BPF_JMP | BPF_OP(BPF_JSLT) | BPF_X, BPF_BRANCH_ARI4), \
  FN_IMM(jslt, 0xc5, BPF_JMP | BPF_OP(BPF_JSLT) | BPF_K, BPF_BRANCH_ARI4), \
  FN_SRC(jsle, 0xdd, BPF_JMP | BPF_OP(BPF_JSLE) | BPF_X, BPF_BRANCH_ARI4), \
  FN_IMM(jsle, 0xd5, BPF_JMP | BPF_OP(BPF_JSLE) | BPF_K, BPF_BRANCH_ARI4), \
  FN_SRC(call, 0x85, BPF_JMP | BPF_CALL, BPF_CALL_ARI2), \
  FN_SRC(exit, 0x95, BPF_JMP | BPF_EXIT, BPF_EXIT_ARI1), \

#endif
// XXX The 2x3 byteswap insns are not too useful.
//     They need special handling since the opcode name determines imm value.
// XXX The 8 ldabs* / ldind* opcodes are specific to network processing.

void
init_bpf_opcode_tables()
{
#define __BPF_SET_OPCODE_NAME(name, x, _x, _cat) bpf_opcode_name_map[(x)] = #name
#define __BPF_SET_OPCODE_SRC(name, x, _x, _cat) bpf_src_opcode_map[#name] = (x)
#define __BPF_SET_OPCODE_IMM(name, x, _x, _cat) bpf_imm_opcode_map[#name] = (x)
#define __BPF_SET_OPCODE_CATEGORY(name, x, _x, cat) bpf_opcode_category_map[(x)] = (cat)
#define __BPF_CHECK_OPCODE(name, x, y, _cat) assert((x)==(y))
  __BPF_OPCODE_MAPPER(__BPF_SET_OPCODE_NAME,__BPF_SET_OPCODE_NAME)
  __BPF_OPCODE_MAPPER(__BPF_SET_OPCODE_SRC,__BPF_SET_OPCODE_IMM)
  __BPF_OPCODE_MAPPER(__BPF_SET_OPCODE_CATEGORY,__BPF_SET_OPCODE_CATEGORY)
  __BPF_OPCODE_MAPPER(__BPF_CHECK_OPCODE,__BPF_CHECK_OPCODE)
  (void)0;
}

/* Convert opcode code to name. */
const char *
bpf_opcode_name(opcode code)
{
  auto it = bpf_opcode_name_map.find(code);
  if (it == bpf_opcode_name_map.end())
    return "unknown";
  return it->second;
}

/* Convert opcode name to code. In ambiguous cases
   e.g. add (0x07 vs 0x0f), prefer the variant that takes a
   register. */
opcode
bpf_opcode_id(const std::string &name)
{
  auto it = bpf_src_opcode_map.find(name);
  if (it == bpf_src_opcode_map.end())
    return 0;
  return it->second;
}

/* If op is an ALU/branch opcode taking src,
   return the equivalent opcode taking imm. */
opcode
bpf_opcode_variant_imm(opcode code)
{
  if (BPF_CLASS(code) == BPF_ALU64
      || BPF_CLASS(code) == BPF_ALU
      || BPF_CLASS(code) == BPF_JMP)
      return (code & ~BPF_X);
  return code;
}

unsigned
bpf_opcode_category(opcode code)
{
  auto it = bpf_opcode_category_map.find(code);
  if (it == bpf_opcode_category_map.end())
    return BPF_UNKNOWN_ARI;
  return it->second;
}

const char *
bpf_expected_args (unsigned cat)
{
  switch (cat) {
  case BPF_MEMORY_ARI4:
  case BPF_BRANCH_ARI4:
    return "3-4";
  case BPF_MEMORY_ARI34_SRCOFF:
  case BPF_MEMORY_ARI34_DSTOFF:
    return "2-4";
  case BPF_ALU_ARI3:
  case BPF_MEMORY_ARI3:
    return "2/4";
  case BPF_ALU_ARI2:
  case BPF_BRANCH_ARI2:
  case BPF_CALL_ARI2:
    return "1/4";
  case BPF_EXIT_ARI1:
    return "0/4";
  case BPF_UNKNOWN_ARI:
  default:
    return "4";
  }
}

/* BPF helper lookup for the translator: */

std::map<unsigned, const char *> bpf_func_name_map;
std::map<std::string, bpf_func_id> bpf_func_id_map;

/* PR23829: On older kernels, bpf.h does not define __BPF_FUNC_MAPPER.
   As a fallback, use the *earliest* __BPF_FUNC_MAPPER, so stapbpf
   will not try helpers that only exist on subsequent kernels.

   TODO: This isn't perfect since even older kernels don't have
   some of these helpers.

   XXX: Note the build limitation in that SystemTap must be compiled
   against a recent kernel to be able to use the helpers from that
   kernel. That's also the case when building against recent bpf.h
   with __BPF_FUNC_MAPPER, so this workaround is not the source of the
   problem. */
#ifndef __BPF_FUNC_MAPPER
#define __BPF_FUNC_MAPPER(FN)		\
	FN(unspec),			\
	FN(map_lookup_elem),		\
	FN(map_update_elem),		\
	FN(map_delete_elem),		\
	FN(probe_read),			\
	FN(ktime_get_ns),		\
	FN(trace_printk),		\
	FN(get_prandom_u32),		\
	FN(get_smp_processor_id),	\
	FN(skb_store_bytes),		\
	FN(l3_csum_replace),		\
	FN(l4_csum_replace),		\
	FN(tail_call),			\
	FN(clone_redirect),		\
	FN(get_current_pid_tgid),	\
	FN(get_current_uid_gid),	\
	FN(get_current_comm),		\
	FN(get_cgroup_classid),		\
	FN(skb_vlan_push),		\
	FN(skb_vlan_pop),		\
	FN(skb_get_tunnel_key),		\
	FN(skb_set_tunnel_key),		\
	FN(perf_event_read),		\
	FN(redirect),			\
	FN(get_route_realm),		\
	FN(perf_event_output),		\
	FN(skb_load_bytes),		\
	FN(get_stackid),		\
	FN(csum_diff),			\
	FN(skb_get_tunnel_opt),		\
	FN(skb_set_tunnel_opt),		\
	FN(skb_change_proto),		\
	FN(skb_change_type),		\
	FN(skb_under_cgroup),		\
	FN(get_hash_recalc),		\
	FN(get_current_task),		\
	FN(probe_write_user),		\
	FN(current_task_under_cgroup),	\
	FN(skb_change_tail),		\
	FN(skb_pull_data),		\
	FN(csum_update),		\
	FN(set_hash_invalid),           \

#endif

void
init_bpf_helper_tables ()
{
#define __BPF_SET_FUNC_NAME(x) bpf_func_name_map[BPF_FUNC_ ## x] = #x
#define __BPF_SET_FUNC_ID(x) bpf_func_id_map[#x] = BPF_FUNC_ ## x
  __BPF_FUNC_MAPPER(__BPF_SET_FUNC_NAME)
  __STAPBPF_FUNC_MAPPER(__BPF_SET_FUNC_NAME)
  __BPF_FUNC_MAPPER(__BPF_SET_FUNC_ID)
  __STAPBPF_FUNC_MAPPER(__BPF_SET_FUNC_ID)
  (void)0;
}

const char *
bpf_function_name (unsigned id)
{
  if (bpf_func_name_map.count(id) != 0)
    return bpf_func_name_map[id];
  return NULL;
}

bpf_func_id
bpf_function_id (const std::string& name)
{
  if (bpf_func_id_map.count(name) != 0)
    return bpf_func_id_map[name];
  return __BPF_FUNC_MAX_ID;
}

unsigned
bpf_function_nargs (unsigned id)
{
  // ??? generalize to all bpf functions
  switch (id)
    {
    case BPF_FUNC_map_lookup_elem:	return 2;
    case BPF_FUNC_map_update_elem:	return 4;
    case BPF_FUNC_map_delete_elem:	return 2;
    case BPF_FUNC_probe_read:		return 3;
    case BPF_FUNC_ktime_get_ns:		return 0;
    case BPF_FUNC_trace_printk:		return 5;
    case BPF_FUNC_get_prandom_u32:	return 0;
    case BPF_FUNC_get_smp_processor_id:	return 0;
    case BPF_FUNC_get_current_pid_tgid:	return 0;
    case BPF_FUNC_get_current_uid_gid:	return 0;
    case BPF_FUNC_get_current_comm:	return 2;
    case BPF_FUNC_perf_event_read:	return 2;
    case BPF_FUNC_perf_event_output:	return 5;
    default:				return 5;
    }
}


void
insn::mark_sets(bitset::set1_ref &s, bool v) const
{
  if (is_call())
    {
      // Return value and call-clobbered registers.
      for (unsigned i = BPF_REG_0; i <= BPF_REG_5; ++i)
	s.set(i, v);
    }
  else if (dest)
    s.set(dest->reg(), v);
}

void
insn::mark_uses(bitset::set1_ref &s, bool v) const
{
  if (is_call())
    {
      unsigned n = off;
      for (unsigned i = 0; i < n; ++i)
	s.set(BPF_REG_1 + i, v);
    }
  else if (code == (BPF_JMP | BPF_EXIT))
    s.set(BPF_REG_0, v);
  else
    {
      if (src0 && src0->is_reg())
	s.set(src0->reg(), v);
      if (src1 && src1->is_reg())
	s.set(src1->reg(), v);
    }
}

static const char *
opcode_name(opcode op)
{
  const char *opn;

  switch (op)
    {
    case BPF_LDX | BPF_MEM | BPF_B:	opn = "ldxb"; break;
    case BPF_LDX | BPF_MEM | BPF_H:	opn = "ldxh"; break;
    case BPF_LDX | BPF_MEM | BPF_W:	opn = "ldxw"; break;
    case BPF_LDX | BPF_MEM | BPF_DW:	opn = "ldx"; break;

    case BPF_STX | BPF_MEM | BPF_B:	opn = "stxb"; break;
    case BPF_STX | BPF_MEM | BPF_H:	opn = "stxh"; break;
    case BPF_STX | BPF_MEM | BPF_W:	opn = "stxw"; break;
    case BPF_STX | BPF_MEM | BPF_DW:	opn = "stx"; break;

    case BPF_ST | BPF_MEM | BPF_B:	opn = "stkb"; break;
    case BPF_ST | BPF_MEM | BPF_H:	opn = "stkh"; break;
    case BPF_ST | BPF_MEM | BPF_W:	opn = "stkw"; break;
    case BPF_ST | BPF_MEM | BPF_DW:	opn = "stk"; break;

    case BPF_ALU64 | BPF_ADD | BPF_X:	opn = "addx"; break;
    case BPF_ALU64 | BPF_ADD | BPF_K:	opn = "addk"; break;
    case BPF_ALU64 | BPF_SUB | BPF_X:	opn = "subx"; break;
    case BPF_ALU64 | BPF_SUB | BPF_K:	opn = "subk"; break;
    case BPF_ALU64 | BPF_AND | BPF_X:	opn = "andx"; break;
    case BPF_ALU64 | BPF_AND | BPF_K:	opn = "andk"; break;
    case BPF_ALU64 | BPF_OR  | BPF_X:	opn = "orx"; break;
    case BPF_ALU64 | BPF_OR  | BPF_K:	opn = "ork"; break;
    case BPF_ALU64 | BPF_LSH | BPF_X:	opn = "lshx"; break;
    case BPF_ALU64 | BPF_LSH | BPF_K:	opn = "lshk"; break;
    case BPF_ALU64 | BPF_RSH | BPF_X:	opn = "rshx"; break;
    case BPF_ALU64 | BPF_RSH | BPF_K:	opn = "rshk"; break;
    case BPF_ALU64 | BPF_XOR | BPF_X:	opn = "xorx"; break;
    case BPF_ALU64 | BPF_XOR | BPF_K:	opn = "xork"; break;
    case BPF_ALU64 | BPF_MUL | BPF_X:	opn = "mulx"; break;
    case BPF_ALU64 | BPF_MUL | BPF_K:	opn = "mulk"; break;
    case BPF_ALU64 | BPF_MOV | BPF_X:	opn = "movx"; break;
    case BPF_ALU64 | BPF_MOV | BPF_K:	opn = "movk"; break;
    case BPF_ALU64 | BPF_ARSH | BPF_X:	opn = "arshx"; break;
    case BPF_ALU64 | BPF_ARSH | BPF_K:	opn = "arshk"; break;
    case BPF_ALU64 | BPF_DIV | BPF_X:	opn = "divx"; break;
    case BPF_ALU64 | BPF_DIV | BPF_K:	opn = "divk"; break;
    case BPF_ALU64 | BPF_MOD | BPF_X:	opn = "modx"; break;
    case BPF_ALU64 | BPF_MOD | BPF_K:	opn = "modk"; break;
    case BPF_ALU64 | BPF_NEG:		opn = "negx"; break;

    case BPF_ALU | BPF_MOV | BPF_X:	opn = "movwx"; break;
    case BPF_ALU | BPF_MOV | BPF_K:	opn = "movwk"; break;

    case BPF_LD | BPF_IMM | BPF_DW:	opn = "movdk"; break;
    case BPF_LD_MAP:			opn = "movmap"; break;

    case BPF_JMP | BPF_CALL:		opn = "call"; break;
    case BPF_JMP | BPF_CALL | BPF_X:	opn = "tcall"; break;
    case BPF_JMP | BPF_EXIT:		opn = "exit"; break;

    case BPF_JMP | BPF_JA:		opn = "jmp"; break;
    case BPF_JMP | BPF_JEQ | BPF_X:	opn = "jeqx"; break;
    case BPF_JMP | BPF_JEQ | BPF_K:	opn = "jeqk"; break;
    case BPF_JMP | BPF_JNE | BPF_X:	opn = "jnex"; break;
    case BPF_JMP | BPF_JNE | BPF_K:	opn = "jnek"; break;
    case BPF_JMP | BPF_JGT | BPF_X:	opn = "jugtx"; break;
    case BPF_JMP | BPF_JGT | BPF_K:	opn = "jugtk"; break;
    case BPF_JMP | BPF_JGE | BPF_X:	opn = "jugex"; break;
    case BPF_JMP | BPF_JGE | BPF_K:	opn = "jugek"; break;
    case BPF_JMP | BPF_JSGT | BPF_X:	opn = "jsgtx"; break;
    case BPF_JMP | BPF_JSGT | BPF_K:	opn = "jsgtk"; break;
    case BPF_JMP | BPF_JSGE | BPF_X:	opn = "jsgex"; break;
    case BPF_JMP | BPF_JSGE | BPF_K:	opn = "jsgek"; break;
    case BPF_JMP | BPF_JSET | BPF_X:	opn = "jsetx"; break;
    case BPF_JMP | BPF_JSET | BPF_K:	opn = "jsetk"; break;

    default:
      opn = "<BUG:unknown opcode>";
    }

  return opn;
}

std::ostream &
insn::print(std::ostream &o) const
{
#ifdef DEBUG_CODEGEN
  if (note != "")
    o << "{" << note << "} ";
#endif
  const char *opn = opcode_name (code);

  switch (code)
    {
    case BPF_LDX | BPF_MEM | BPF_B:
    case BPF_LDX | BPF_MEM | BPF_H:
    case BPF_LDX | BPF_MEM | BPF_W:
    case BPF_LDX | BPF_MEM | BPF_DW:
      return o << opn << "\t" << *dest
	       << ",[" << *src1
	       << showpos << off << noshowpos << "]";

    case BPF_STX | BPF_MEM | BPF_B:
    case BPF_STX | BPF_MEM | BPF_H:
    case BPF_STX | BPF_MEM | BPF_W:
    case BPF_STX | BPF_MEM | BPF_DW:
    case BPF_ST | BPF_MEM | BPF_B:
    case BPF_ST | BPF_MEM | BPF_H:
    case BPF_ST | BPF_MEM | BPF_W:
    case BPF_ST | BPF_MEM | BPF_DW:
      return o << opn << "\t[" << *src0
	       << showpos << off << noshowpos
	       << "]," << *src1;

    case BPF_ALU | BPF_MOV | BPF_X:
    case BPF_ALU | BPF_MOV | BPF_K:
    case BPF_ALU64 | BPF_MOV | BPF_X:
    case BPF_ALU64 | BPF_MOV | BPF_K:
    case BPF_LD | BPF_IMM | BPF_DW:
    case BPF_LD_MAP:
      return o << opn << "\t" << *dest << "," << *src1;

    case BPF_ALU64 | BPF_NEG:
      return o << opn << "\t" << *dest << "," << *src0;

    case BPF_ALU64 | BPF_ADD | BPF_X:
    case BPF_ALU64 | BPF_ADD | BPF_K:
    case BPF_ALU64 | BPF_SUB | BPF_X:
    case BPF_ALU64 | BPF_SUB | BPF_K:
    case BPF_ALU64 | BPF_AND | BPF_X:
    case BPF_ALU64 | BPF_AND | BPF_K:
    case BPF_ALU64 | BPF_OR  | BPF_X:
    case BPF_ALU64 | BPF_OR  | BPF_K:
    case BPF_ALU64 | BPF_LSH | BPF_X:
    case BPF_ALU64 | BPF_LSH | BPF_K:
    case BPF_ALU64 | BPF_RSH | BPF_X:
    case BPF_ALU64 | BPF_RSH | BPF_K:
    case BPF_ALU64 | BPF_XOR | BPF_X:
    case BPF_ALU64 | BPF_XOR | BPF_K:
    case BPF_ALU64 | BPF_MUL | BPF_X:
    case BPF_ALU64 | BPF_MUL | BPF_K:
    case BPF_ALU64 | BPF_ARSH | BPF_X:
    case BPF_ALU64 | BPF_ARSH | BPF_K:
    case BPF_ALU64 | BPF_DIV | BPF_X:
    case BPF_ALU64 | BPF_DIV | BPF_K:
    case BPF_ALU64 | BPF_MOD | BPF_K:
    case BPF_ALU64 | BPF_MOD | BPF_X:
      return o << opn << "\t" << *dest << "," << *src0 << "," << *src1;

    case BPF_JMP | BPF_CALL:
    case BPF_JMP | BPF_CALL | BPF_X:
      o << opn << "\t";
      if (const char *name = bpf_function_name(src1->imm()))
	o << name;
      else
	o << *src1;
      return o << "," << off;

    case BPF_JMP | BPF_EXIT:
    case BPF_JMP | BPF_JA:
      return o << opn;

    case BPF_JMP | BPF_JEQ | BPF_X:
    case BPF_JMP | BPF_JEQ | BPF_K:
    case BPF_JMP | BPF_JNE | BPF_X:
    case BPF_JMP | BPF_JNE | BPF_K:
    case BPF_JMP | BPF_JGT | BPF_X:
    case BPF_JMP | BPF_JGT | BPF_K:
    case BPF_JMP | BPF_JGE | BPF_X:
    case BPF_JMP | BPF_JGE | BPF_K:
    case BPF_JMP | BPF_JSGT | BPF_X:
    case BPF_JMP | BPF_JSGT | BPF_K:
    case BPF_JMP | BPF_JSGE | BPF_X:
    case BPF_JMP | BPF_JSGE | BPF_K:
    case BPF_JMP | BPF_JSET | BPF_X:
    case BPF_JMP | BPF_JSET | BPF_K:
      return o << opn << "\t" << *src0 << "," << *src1;

    default:
      return o << "<BUG:unknown instruction format>";
    }
}

edge::edge(block *p, block *n)
  : prev(p), next(n)
{
  n->prevs.insert (this);
}

edge::~edge()
{
  next->prevs.erase (this);
  if (prev->taken == this)
    prev->taken = NULL;
  if (prev->fallthru == this)
    prev->fallthru = NULL;
}

void
edge::redirect_next(block *n)
{
  next->prevs.erase (this);
  next = n;
  n->prevs.insert (this);
}

block::block(int i)
  : first(NULL), last(NULL), taken(NULL), fallthru(NULL), id(i)
{ }

block::~block()
{
  for (insn *n, *i = first; i ; i = n)
    {
      n = i->next;
      delete i;
    }
  delete taken;
  delete fallthru;
}

block *
block::is_forwarder() const
{
  if (first == NULL)
    {
      if (fallthru)
	return fallthru->next;
    }
  else if (first == last && first->code == (BPF_JMP | BPF_JA))
    return taken->next;
  return NULL;
}

void
block::print(ostream &o) const
{
  if (prevs.empty ())
    o << "\t[prevs: entry]\n";
  else
    {
      o << "\t[prevs:";
      for (edge_set::const_iterator i = prevs.begin(); i != prevs.end(); ++i)
	o << ' ' << (*i)->prev->id;
      o << "]\n";
    }

  o << id << ':' << endl;
  for (insn *i = first; i != NULL; i = i->next)
    o << '\t' << *i << endl;

  if (taken)
    o << "\t[taken: " << taken->next->id << "]" << endl;
  if (fallthru)
    o << "\t[fallthru: " << fallthru->next->id << "]" << endl;
  else if (!taken)
    o << "\t[end]" << endl;
}

insn *
insn_inserter::new_insn()
{
  insn *n = new insn;
#ifdef DEBUG_CODEGEN
  if (!notes.empty())
    n->note = notes.top();
  else
    n->note = "";
#endif
  insert(n);
  return n;
}

void
insn_before_inserter::insert(insn *n)
{
  assert(i != NULL);
  insn *p = i->prev;
  i->prev = n;
  n->prev = p;
  n->next = i;
  if (p == NULL)
    b->first = n;
  else
    p->next = n;
}

void
insn_after_inserter::insert(insn *p)
{
  if (i == NULL)
    {
      assert(b->first == NULL && b->last == NULL);
      b->first = b->last = p;
    }
  else
    {
      insn *n = i->next;
      i->next = p;
      p->prev = i;
      p->next = n;
      if (n == NULL)
	b->last = p;
      else
	n->prev = p;
    }
  i = p;
}

program::program(enum bpf_target target)
  : target(target), hardreg_vals(MAX_BPF_REG),
    max_tmp_space(0), max_reg_space(0)
{
  for (unsigned i = 0; i < MAX_BPF_REG; ++i)
    hardreg_vals[i] = value::mk_hardreg(i);
}

program::~program()
{
  // XXX We need to suffer a memory leak here, as blocks / edges are
  // tightly interlinked structures, and their dtors like to invoke
  // functions on each other.  This will need a rethink, as this is
  // the type of problem domain where a garbage collected runtime
  // shines, and most other languages don't.
  #if 0
  for (auto i = blocks.begin (); i != blocks.end (); ++i)
    delete *i;
  for (auto i = reg_vals.begin (); i != reg_vals.end (); ++i)
    delete *i;
  for (auto i = imm_map.begin (); i != imm_map.end (); ++i)
    delete i->second;
  for (auto i = str_map.begin (); i != str_map.end (); ++i)
    delete i->second;
  #endif
}

block *
program::new_block ()
{
  block *r = new block(blocks.size ());
  blocks.push_back (r);
  return r;
}

value *
program::lookup_reg(regno r)
{
  if (r < MAX_BPF_REG)
    return &hardreg_vals[r];
  else
    return reg_vals[r - MAX_BPF_REG];
}

value *
program::new_reg()
{
  regno r = max_reg();
  value *v = new value(value::mk_reg(r));
  reg_vals.push_back(v);
  return v;
}

value *
program::new_imm(int64_t i)
{
  auto old = imm_map.find(i);
  if (old != imm_map.end())
    return old->second;

  value *v = new value(value::mk_imm(i));
  auto ok = imm_map.insert(std::pair<int64_t, value *>(i, v));
  assert(ok.second);
  return v;
}

value *
program::new_str(std::string str, bool format_str)
{
  std::unordered_map<std::string, value *>& m = str_map;
  if (format_str) m = format_map;

  auto old = m.find(str);
  if (old != m.end())
    return old->second;

  value *v = new value(value::mk_str(str, format_str));
  auto ok = m.insert(std::pair<std::string, value *>(str, v));
  assert(ok.second);
  return v;
}

void
program::mk_ld(insn_inserter &ins, int sz, value *dest, value *base, int off)
{
  insn *i = ins.new_insn();
  i->code = BPF_LDX | BPF_MEM | sz;
  i->off = off;
  i->dest = dest;
  i->src1 = base;
}

void
program::mk_st(insn_inserter &ins, int sz, value *base, int off, value *src)
{
  insn *i = ins.new_insn();
  i->code = (src->is_imm() ? BPF_ST : BPF_STX) | BPF_MEM | sz;
  i->off = off;
  i->src0 = base;
  i->src1 = src;
}

void
program::mk_binary(insn_inserter &ins, opcode op, value *dest,
		   value *s0, value *s1)
{
  if (op == BPF_SUB)
    {
      if (s0->is_imm() && s0->imm() == 0)
	{
	  mk_unary(ins, BPF_NEG, dest, s1);
	  return;
	}
    }
  else if (is_commutative(op)
	   && ((s1->is_reg() && !s0->is_reg()) || dest == s1))
    std::swap (s1, s0);

  insn *i = ins.new_insn();
  i->code = BPF_ALU64 | op | (s1->is_imm() ? BPF_K : BPF_X);
  i->dest = dest;
  i->src0 = s0;
  i->src1 = s1;
}

void
program::mk_unary(insn_inserter &ins, opcode op, value *dest, value *src)
{
  assert (op == BPF_NEG); // XXX: BPF_NEG is the only unary operator so far.

  if (dest != src) // src is not used for BPF_NEG. BPF negates in-place.
    mk_mov(ins, dest, src);

  insn *i = ins.new_insn();
  i->code = BPF_ALU64 | op; // BPF_X is not used for BPF_NEG.
  i->dest = dest;
  i->src0 = dest; // XXX: dest as an ersatz 'source'.
}

void
program::mk_mov(insn_inserter &ins, value *dest, value *src)
{
  if (dest == src)
    return;

  opcode code = BPF_ALU64 | BPF_MOV | BPF_X;
  if (src->is_imm())
    {
      int64_t i = src->imm();
      if (i == (int32_t)i)
	code = BPF_ALU64 | BPF_MOV | BPF_K;
      else if (i == (uint32_t)i)
	code = BPF_ALU | BPF_MOV | BPF_K;
      else
	code = BPF_LD | BPF_IMM | BPF_DW;
    }

  insn *i = ins.new_insn();
  i->code = code;
  i->dest = dest;
  i->src1 = src;
}

void
program::mk_jmp(insn_inserter &ins, block *dest)
{
  insn *i = ins.new_insn();
  i->code = BPF_JMP | BPF_JA;

  block *b = ins.get_block();
  b->taken = new edge(b, dest);
}

void
program::mk_call(insn_inserter &ins, enum bpf_func_id id, unsigned nargs)
{
  insn *i = ins.new_insn();
  i->code = BPF_JMP | BPF_CALL;
  i->src1 = new_imm((int)id);
  i->off = nargs;
}

void
program::mk_exit(insn_inserter &ins)
{
  insn *i = ins.new_insn();
  i->code = BPF_JMP | BPF_EXIT;
}

void
program::mk_jcond(insn_inserter &ins, condition c, value *s0, value *s1,
		  block *t, block *f)
{
  bool inv = false;
  opcode code;

  if (s1->is_reg() && !s0->is_reg())
    {
      std::swap (s1, s0);
      switch (c)
	{
	case EQ:	break;
	case NE:	break;
	case TEST:	break;
	case LT:	c = GT; break;
	case LE:	c = GE; break;
	case GT:	c = LT; break;
	case GE:	c = LE; break;
	case LTU:	c = GTU; break;
	case LEU:	c = GEU; break;
	case GTU:	c = LTU; break;
	case GEU:	c = LEU; break;
	default:	abort();
	}
    }

  switch (c)
    {
    case EQ:
      code = BPF_JEQ;
      break;
    case NE:
      code = BPF_JNE;
      break;
    case LE:
      inv = true;
      /* Fallthrough */
    case GT:
      code = BPF_JSGT;
      break;
    case LT:
      inv = true;
      /* Fallthrough */
    case GE:
      code = BPF_JSGE;
      break;
    case LEU:
      inv = true;
      /* Fallthrough */
    case GTU:
      code = BPF_JGT;
      break;
    case LTU:
      inv = true;
      /* Fallthrough */
    case GEU:
      code = BPF_JGE;
      break;
    case TEST:
      code = BPF_JSET;
      break;
    default:
      abort ();
    }

  if (inv)
    std::swap (t, f);

  block *b = ins.get_block();
  b->taken = new edge(b, t);
  b->fallthru = new edge(b, f);

  insn *i = ins.new_insn();
  i->code = BPF_JMP | code | (s1->is_imm() ? BPF_K : BPF_X);
  i->src0 = s0;
  i->src1 = s1;
}

void
program::load_map(insn_inserter &ins, value *dest, int src)
{
  assert (src >= 0); // PR23476: Ensure a stray stats reference doesn't slip through.
  insn *i = ins.new_insn();
  i->code = BPF_LD_MAP;
  i->dest = dest;
  i->src1 = new_imm(src);
}

void
program::print(ostream &o) const
{
  for (unsigned n = blocks.size(), i = 0; i < n; ++i)
    {
      block *b = blocks[i];
      if (b)
	o << *b << endl;
    }
}
} // namespace bpf
