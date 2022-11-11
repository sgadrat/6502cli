#include "mos6502.h"

#include <array>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <readline/readline.h>
#include <readline/history.h>
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
	std::cout
		<< "A=" << uint16_t(emu.A) << " X=" << uint16_t(emu.X) << " Y=" << uint16_t(emu.Y) << '\n'
	;
}

static bool handle_pseudo_opcode(std::string const& line, mos6502& emu) {
	if (line == "%cpu") {
		show_cpu(emu);
	}else {
		std::cerr << "unknown pseudo opcode " << line << '\n';
	}

	return true;
}

static bool handle_assembly(std::string const& line, mos6502& emu) {
	// Assemble the line
	{
		std::ofstream ofs("/tmp/6502cli.tmp.asm");
		ofs << line << '\n';
	}
	int ret = ::system("xa /tmp/6502cli.tmp.asm -o /tmp/6502cli.tmp.compiled");
	if (WIFSIGNALED(ret) && (WTERMSIG(ret) == SIGINT || WTERMSIG(ret) == SIGQUIT)) {
		return false;
	}

	size_t const max_compiled_size = 100;
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
