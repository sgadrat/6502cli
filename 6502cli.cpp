#include "mos6502.h"

#include <array>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iostream>
#include <readline/readline.h>
#include <readline/history.h>
#include <sstream>
#include <string>

// g++ 6502cli.cpp mos6502.cpp -lreadline -o 6502cli

std::array<uint8_t, 0x10000> memory;

void write_memory(uint16_t addr, uint8_t value) {
	memory[addr] = value;
}

uint8_t read_memory(uint16_t addr) {
	if (addr == 0xffff) {
		throw mos6502::Break();
	}
	return memory[addr];
}

void show_cpu(mos6502 const &emu) {
	auto s = [&](uint8_t mask) {
		return (emu.status & mask) != 0;
	};

	std::cout
		<< "A=" << uint16_t(emu.A) << " X=" << uint16_t(emu.X) << " Y=" << uint16_t(emu.Y) << '\n'
		<< "SP=" << uint16_t(emu.sp) << " PC=" << emu.pc << '\n'
		<< "C=" << uint16_t(s(CARRY)) << " Z=" << uint16_t(s(ZERO)) << " I=" << uint16_t(s(INTERRUPT)) << " D=" << uint16_t(s(DECIMAL)) << " B=" << uint16_t(s(BREAK)) << " V=" << uint16_t(s(OVERFLOW)) << " N=" << uint16_t(s(NEGATIVE)) << '\n'
	;
}

static bool handle_assembly(std::string const& line, mos6502& emu);

static bool handle_pseudo_opcode(std::string const& line, mos6502& emu) {
	std::istringstream line_reader(line);
	std::string opcode;
	line_reader >> opcode;

	if (opcode == "%cpu") {
		show_cpu(emu);
	}else if (opcode == "%mem") {
		// Parse arguments
		size_t offset;
		size_t length;
		line_reader >> offset >> length;
		std::ostringstream out;
		out << std::hex << std::setfill('0') << std::setw(2);
		for (size_t i = offset; i < offset + length; ++i) {
			out << uint16_t(memory[i]);
			if (i < offset + length - 1) out << ' ';
		}
		std::cout << out.str() << '\n';
	}else if (opcode == "%asm") {
		// Parse arguments
		std::string source_path;
		line_reader >> source_path;

		// Read file
		std::ifstream source_reader(source_path);
		if (source_reader.fail()) {
			std::cerr << "ERROR: failed to open '" << source_path << "'\n";
			return true;
		}
		source_reader.seekg(0, std::ios_base::end);
		auto const source_size = source_reader.tellg();
		source_reader.seekg(0);

		std::string source(source_size, '\0');
		source_reader.read(source.data(), source_size);

		// Display source
		std::cout << source << '\n';

		// Interprete source
		handle_assembly(source, emu);
	}else {
		std::cerr << "unknown pseudo opcode '" << opcode << "'\n";
	}

	return true;
}

static bool handle_assembly(std::string const& source, mos6502& emu) {
	// Assemble the source code
	{
		std::ofstream ofs("/tmp/6502cli.tmp.asm");
		ofs << source << '\n';
	}
	int ret = ::system("xa /tmp/6502cli.tmp.asm -o /tmp/6502cli.tmp.compiled");
	if (WIFSIGNALED(ret) && (WTERMSIG(ret) == SIGINT || WTERMSIG(ret) == SIGQUIT)) {
		return false;
	}

	size_t const max_compiled_size = 4096; // Start your scripts with: * = $f000
	std::array<char, max_compiled_size> compiled = {char(0xea)}; // 0xea == NOP

	std::ifstream compiled_reader("/tmp/6502cli.tmp.compiled");
	compiled_reader.read(compiled.data(), max_compiled_size);

	// Run emulator on assembled code
	size_t const compiled_code_offset = memory.size() - compiled.size();
	std::memcpy(memory.data() + compiled_code_offset, compiled.data(), compiled.size());

	uint32_t max_cycles = 1'000'000;
	uint64_t cycles_count = 0;
	emu.pc = compiled_code_offset;
	emu.Run(max_cycles, cycles_count, mos6502::CYCLE_COUNT);

	return true;
}

int main() {
	// Init emulator
	memory.fill(0);
	mos6502 emu(&read_memory, &write_memory);

	// Run lines
	while (true) {
		// Fetch line from stdin
		char* cline = readline(">>> ");
		if (!cline) {
			std::cout << '\n';
			break;
		}
		add_history(cline);
		std::string line(cline);
		free(cline);

		// Select action
		bool ok = true;
		switch (line[0]) {
			case '%':
				ok = handle_pseudo_opcode(line, emu);
				break;
			default:
				ok = handle_assembly(line, emu);
				break;
		};

		if (!ok) {
			break;
		}
	}
}
