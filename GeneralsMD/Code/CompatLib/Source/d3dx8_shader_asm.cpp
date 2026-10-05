// GeneralsX @bugfix ishassan 05/10/2026 Assemble ps.1.0 - ps.1.3 pixel shaders without D3DX.
// The game assembles its water shaders (river, texbem water, trapezoid water with sparkles) at run
// time with D3DXAssembleShader. On Linux and macOS there is no D3DX, so the water was drawn without
// these shaders. This file turns the shader text into the D3D8 token stream that CreatePixelShader
// takes. It supports the instructions, registers and modifiers of pixel shader versions 1.0 to 1.3.

#include "d3dx8core.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Token values from the D3D8 shader token format.
enum : DWORD
{
	OP_NOP = 0,
	OP_MOV = 1,
	OP_ADD = 2,
	OP_SUB = 3,
	OP_MAD = 4,
	OP_MUL = 5,
	OP_DP3 = 8,
	OP_DP4 = 9,
	OP_LRP = 18,
	OP_TEXCOORD = 64,
	OP_TEXKILL = 65,
	OP_TEX = 66,
	OP_TEXBEM = 67,
	OP_TEXBEML = 68,
	OP_TEXREG2AR = 69,
	OP_TEXREG2GB = 70,
	OP_TEXM3x2PAD = 71,
	OP_TEXM3x2TEX = 72,
	OP_TEXM3x3PAD = 73,
	OP_TEXM3x3TEX = 74,
	OP_TEXM3x3SPEC = 76,
	OP_TEXM3x3VSPEC = 77,
	OP_CND = 80,
	OP_DEF = 81,
	OP_TEXREG2RGB = 82,
	OP_TEXDP3TEX = 83,
	OP_TEXM3x2DEPTH = 84,
	OP_TEXDP3 = 85,
	OP_TEXM3x3 = 86,
	OP_END = 0xFFFF,
};

enum : DWORD
{
	REG_TEMP = 0,
	REG_INPUT = 1,
	REG_CONST = 2,
	REG_TEXTURE = 3,
};

enum : DWORD
{
	SRCMOD_NONE = 0,
	SRCMOD_NEG = 1,
	SRCMOD_BIAS = 2,
	SRCMOD_BIASNEG = 3,
	SRCMOD_SIGN = 4,
	SRCMOD_SIGNNEG = 5,
	SRCMOD_COMP = 6,
};

const DWORD PARAM_TOKEN = 0x80000000;
const DWORD COISSUE = 0x40000000;
const DWORD RESULT_SATURATE = 0x00100000;
const DWORD SWIZZLE_IDENTITY = 0xE4;

struct OpcodeInfo
{
	const char *name;
	DWORD opcode;
	int sourceCount;
};

const OpcodeInfo OPCODES[] =
{
	{ "nop", OP_NOP, -1 },
	{ "mov", OP_MOV, 1 },
	{ "add", OP_ADD, 2 },
	{ "sub", OP_SUB, 2 },
	{ "mad", OP_MAD, 3 },
	{ "mul", OP_MUL, 2 },
	{ "dp3", OP_DP3, 2 },
	{ "dp4", OP_DP4, 2 },
	{ "lrp", OP_LRP, 3 },
	{ "cnd", OP_CND, 3 },
	{ "def", OP_DEF, 4 },
	{ "texcoord", OP_TEXCOORD, 0 },
	{ "texkill", OP_TEXKILL, 0 },
	{ "tex", OP_TEX, 0 },
	{ "texbem", OP_TEXBEM, 1 },
	{ "texbeml", OP_TEXBEML, 1 },
	{ "texreg2ar", OP_TEXREG2AR, 1 },
	{ "texreg2gb", OP_TEXREG2GB, 1 },
	{ "texreg2rgb", OP_TEXREG2RGB, 1 },
	{ "texm3x2pad", OP_TEXM3x2PAD, 1 },
	{ "texm3x2tex", OP_TEXM3x2TEX, 1 },
	{ "texm3x2depth", OP_TEXM3x2DEPTH, 1 },
	{ "texm3x3pad", OP_TEXM3x3PAD, 1 },
	{ "texm3x3tex", OP_TEXM3x3TEX, 1 },
	{ "texm3x3", OP_TEXM3x3, 1 },
	{ "texm3x3spec", OP_TEXM3x3SPEC, 2 },
	{ "texm3x3vspec", OP_TEXM3x3VSPEC, 1 },
	{ "texdp3tex", OP_TEXDP3TEX, 1 },
	{ "texdp3", OP_TEXDP3, 1 },
};

std::string trim(const std::string &text)
{
	size_t begin = 0;
	size_t end = text.size();
	while (begin < end && isspace((unsigned char)text[begin]))
		++begin;
	while (end > begin && isspace((unsigned char)text[end - 1]))
		--end;
	return text.substr(begin, end - begin);
}

std::string lower(std::string text)
{
	for (char &c : text)
		c = (char)tolower((unsigned char)c);
	return text;
}

std::vector<std::string> splitOperands(const std::string &text)
{
	std::vector<std::string> operands;
	size_t start = 0;
	while (start <= text.size())
	{
		size_t comma = text.find(',', start);
		if (comma == std::string::npos)
			comma = text.size();
		operands.push_back(trim(text.substr(start, comma - start)));
		start = comma + 1;
	}
	return operands;
}

bool parseRegister(const std::string &text, DWORD *type, DWORD *number)
{
	if (text.size() < 2)
		return false;

	switch (text[0])
	{
		case 'r': *type = REG_TEMP; break;
		case 'v': *type = REG_INPUT; break;
		case 'c': *type = REG_CONST; break;
		case 't': *type = REG_TEXTURE; break;
		default: return false;
	}

	char *end = nullptr;
	unsigned long value = strtoul(text.c_str() + 1, &end, 10);
	if (end == text.c_str() + 1 || *end != '\0' || value > 0x7FF)
		return false;

	*number = (DWORD)value;
	return true;
}

DWORD registerBits(DWORD type, DWORD number)
{
	return ((type << 28) & 0x70000000) | ((type << 8) & 0x00001800) | number;
}

// Component letters: r g b a or x y z w.
int componentIndex(char c)
{
	switch (c)
	{
		case 'r': case 'x': return 0;
		case 'g': case 'y': return 1;
		case 'b': case 'z': return 2;
		case 'a': case 'w': return 3;
		default: return -1;
	}
}

bool parseDestination(const std::string &text, DWORD resultModifier, DWORD shift, DWORD *token)
{
	std::string reg = text;
	DWORD mask = 0xF;

	size_t dot = text.find('.');
	if (dot != std::string::npos)
	{
		reg = text.substr(0, dot);
		mask = 0;
		for (size_t i = dot + 1; i < text.size(); ++i)
		{
			int index = componentIndex(text[i]);
			if (index < 0)
				return false;
			mask |= 1u << index;
		}
		if (mask == 0)
			return false;
	}

	DWORD type, number;
	if (!parseRegister(reg, &type, &number))
		return false;

	*token = PARAM_TOKEN | registerBits(type, number) | (mask << 16) | resultModifier | (shift << 24);
	return true;
}

bool parseSource(const std::string &text, DWORD *token)
{
	std::string rest = text;
	bool negate = false;
	bool complement = false;

	if (!rest.empty() && rest[0] == '-')
	{
		negate = true;
		rest = trim(rest.substr(1));
	}
	if (rest.size() > 2 && rest[0] == '1' && rest[1] == '-')
	{
		complement = true;
		rest = trim(rest.substr(2));
	}

	DWORD swizzle = SWIZZLE_IDENTITY;
	size_t dot = rest.find('.');
	if (dot != std::string::npos)
	{
		std::string components = rest.substr(dot + 1);
		rest = rest.substr(0, dot);
		if (components.empty() || components.size() > 4)
			return false;

		// A short swizzle repeats its last component, as in "c0.a" = "c0.aaaa".
		DWORD value = 0;
		int last = 0;
		for (size_t i = 0; i < 4; ++i)
		{
			if (i < components.size())
			{
				last = componentIndex(components[i]);
				if (last < 0)
					return false;
			}
			value |= (DWORD)last << (2 * i);
		}
		swizzle = value;
	}

	DWORD modifier = SRCMOD_NONE;
	size_t underscore = rest.find('_');
	if (underscore != std::string::npos)
	{
		std::string suffix = rest.substr(underscore + 1);
		rest = rest.substr(0, underscore);
		if (suffix == "bias")
			modifier = negate ? SRCMOD_BIASNEG : SRCMOD_BIAS;
		else if (suffix == "bx2")
			modifier = negate ? SRCMOD_SIGNNEG : SRCMOD_SIGN;
		else
			return false;
		if (complement)
			return false;
	}
	else if (complement)
	{
		if (negate)
			return false;
		modifier = SRCMOD_COMP;
	}
	else if (negate)
	{
		modifier = SRCMOD_NEG;
	}

	DWORD type, number;
	if (!parseRegister(rest, &type, &number))
		return false;

	*token = PARAM_TOKEN | registerBits(type, number) | (swizzle << 16) | (modifier << 24);
	return true;
}

bool parseFloat(const std::string &text, DWORD *token)
{
	char *end = nullptr;
	float value = strtof(text.c_str(), &end);
	if (end == text.c_str() || *end != '\0')
		return false;
	memcpy(token, &value, sizeof(value));
	return true;
}

// Instruction modifiers: _sat, _x2, _x4, _x8, _d2, _d4, _d8. The shift is a signed 4-bit value.
bool parseInstructionModifiers(const std::string &text, DWORD *resultModifier, DWORD *shift)
{
	*resultModifier = 0;
	*shift = 0;
	size_t start = 0;
	while (start < text.size())
	{
		size_t next = text.find('_', start);
		if (next == std::string::npos)
			next = text.size();
		std::string modifier = text.substr(start, next - start);
		if (modifier == "sat") *resultModifier = RESULT_SATURATE;
		else if (modifier == "x2") *shift = 1;
		else if (modifier == "x4") *shift = 2;
		else if (modifier == "x8") *shift = 3;
		else if (modifier == "d2") *shift = 0xF;
		else if (modifier == "d4") *shift = 0xE;
		else if (modifier == "d8") *shift = 0xD;
		else return false;
		start = next + 1;
	}
	return true;
}

bool assembleLine(const std::string &line, std::vector<DWORD> &tokens, std::string &error)
{
	std::string text = line;
	DWORD coissue = 0;
	if (!text.empty() && text[0] == '+')
	{
		coissue = COISSUE;
		text = trim(text.substr(1));
	}

	size_t space = text.find_first_of(" \t");
	std::string mnemonic = lower(text.substr(0, space));
	std::string operandText = space == std::string::npos ? std::string() : trim(text.substr(space));

	std::string name = mnemonic;
	std::string modifiers;
	size_t underscore = mnemonic.find('_');
	if (underscore != std::string::npos)
	{
		name = mnemonic.substr(0, underscore);
		modifiers = mnemonic.substr(underscore + 1);
	}

	const OpcodeInfo *info = nullptr;
	for (const OpcodeInfo &candidate : OPCODES)
	{
		if (name == candidate.name)
		{
			info = &candidate;
			break;
		}
	}
	if (info == nullptr)
	{
		error = "unknown instruction: " + mnemonic;
		return false;
	}

	DWORD resultModifier, shift;
	if (!parseInstructionModifiers(modifiers, &resultModifier, &shift))
	{
		error = "unknown instruction modifier: " + mnemonic;
		return false;
	}

	tokens.push_back(info->opcode | coissue);
	if (info->sourceCount < 0)
	{
		if (!operandText.empty())
		{
			error = "nop takes no operands";
			return false;
		}
		return true;
	}

	std::vector<std::string> operands = splitOperands(lower(operandText));
	if ((int)operands.size() != info->sourceCount + 1)
	{
		error = "wrong operand count: " + text;
		return false;
	}

	DWORD token;
	if (!parseDestination(operands[0], resultModifier, shift, &token))
	{
		error = "bad destination register: " + operands[0];
		return false;
	}
	tokens.push_back(token);

	for (int i = 1; i <= info->sourceCount; ++i)
	{
		bool ok = info->opcode == OP_DEF ? parseFloat(operands[i], &token) : parseSource(operands[i], &token);
		if (!ok)
		{
			error = "bad source operand: " + operands[i];
			return false;
		}
		tokens.push_back(token);
	}
	return true;
}

bool assemblePixelShader(const char *source, size_t length, std::vector<DWORD> &tokens, std::string &error)
{
	std::string text(source, length);
	bool haveVersion = false;
	int lineNumber = 0;
	size_t start = 0;

	while (start < text.size())
	{
		size_t end = text.find('\n', start);
		if (end == std::string::npos)
			end = text.size();
		std::string line = text.substr(start, end - start);
		start = end + 1;
		++lineNumber;

		size_t comment = line.find(';');
		if (comment != std::string::npos)
			line.erase(comment);
		comment = line.find("//");
		if (comment != std::string::npos)
			line.erase(comment);
		line = trim(line);
		if (line.empty())
			continue;

		std::string lineError;
		if (!haveVersion)
		{
			std::string version = lower(line);
			if (version.size() != 6 || (version.compare(0, 3, "ps.") != 0 && version.compare(0, 3, "ps_") != 0)
				|| version[3] != '1' || (version[4] != '.' && version[4] != '_') || version[5] < '0' || version[5] > '3')
			{
				error = "line " + std::to_string(lineNumber) + ": only ps.1.0 to ps.1.3 are supported";
				return false;
			}
			tokens.push_back(0xFFFF0100 | (DWORD)(version[5] - '0'));
			haveVersion = true;
			continue;
		}

		if (!assembleLine(line, tokens, lineError))
		{
			error = "line " + std::to_string(lineNumber) + ": " + lineError;
			return false;
		}
	}

	if (!haveVersion)
	{
		error = "missing shader version";
		return false;
	}

	tokens.push_back(OP_END);
	return true;
}

LPD3DXBUFFER createBuffer(const void *data, DWORD size)
{
	LPD3DXBUFFER buffer = new D3DXBUFFER;
	buffer->m_data = new BYTE[size];
	buffer->m_size = size;
	memcpy(buffer->m_data, data, size);
	return buffer;
}

} // namespace

ULONG D3DXBUFFER::Release()
{
	delete[] m_data;
	delete this;
	return 0;
}

HRESULT WINAPI
D3DXAssembleShader(
	LPCVOID pSrcData,
	UINT SrcDataLen,
	DWORD Flags,
	LPD3DXBUFFER *ppConstants,
	LPD3DXBUFFER *ppCompiledShader,
	LPD3DXBUFFER *ppCompilationErrors)
{
	if (ppConstants != nullptr)
		*ppConstants = nullptr;
	if (ppCompiledShader != nullptr)
		*ppCompiledShader = nullptr;
	if (ppCompilationErrors != nullptr)
		*ppCompilationErrors = nullptr;

	if (pSrcData == nullptr || ppCompiledShader == nullptr)
		return D3DERR_INVALIDCALL;

	std::vector<DWORD> tokens;
	std::string error;
	if (!assemblePixelShader((const char *)pSrcData, SrcDataLen, tokens, error))
	{
		if (ppCompilationErrors != nullptr)
			*ppCompilationErrors = createBuffer(error.c_str(), (DWORD)error.size() + 1);
		return D3DERR_INVALIDCALL;
	}

	*ppCompiledShader = createBuffer(tokens.data(), (DWORD)(tokens.size() * sizeof(DWORD)));
	return D3D_OK;
}
