// zscript255: upstream 2.55.17's zq/ffasmexport.h (the editor's disassembler). The
// compiler includes it only for the disassembled_script_data name.
#ifndef ZASM_EXPORT_H
#define ZASM_EXPORT_H

#include "parser/Compiler.h"
#include "base/zdefs.h"

namespace ZScript
{
	class Opcode;
	struct disassembled_script_data;
}
using ZScript::disassembled_script_data;

#endif
