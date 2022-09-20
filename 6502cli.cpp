#include "mos6502.h"

#include <array>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

// g++ 6502cli.cpp mos6502.cpp -o 6502cli

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
		<< "  A=" << uint16_t(emu.A) << " X=" << uint16_t(emu.X) << " Y=" << uint16_t(emu.Y) << '\n'
	;
}

int main() {
	// Init emulator
	memory.fill(0);
	mos6502 emu(&read_memory, &write_memory);
	uint64_t cycles_count = 0;

	// Run lines
	while (true) {
		// Fetch line from stdin
		std::string line;
		std::getline(std::cin, line);
		{
			std::ofstream ofs("/tmp/6502cli.tmp.asm");
			ofs << line << '\n';
		}

		// Assemble the line
		int ret = ::system("xa /tmp/6502cli.tmp.asm -o /tmp/6502cli.tmp.compiled");
		if (WIFSIGNALED(ret) && (WTERMSIG(ret) == SIGINT || WTERMSIG(ret) == SIGQUIT)) {
			break;
		}

		size_t const max_compiled_size = 100;
		std::array<char, max_compiled_size> compiled = {char(0xea)}; // 0xea == NOP

		std::ifstream compiled_reader("/tmp/6502cli.tmp.compiled");
		compiled_reader.read(compiled.data(), max_compiled_size);

		// Run emulator on assembled code
		size_t const compiled_code_offset = memory.size() - max_compiled_size;
		std::memcpy(memory.data() + compiled_code_offset, compiled.data(), max_compiled_size);

		uint32_t max_cycles = 1'000'000;
		emu.pc = compiled_code_offset;
		emu.Run(max_cycles, cycles_count, mos6502::CYCLE_COUNT);

		// Show CPU state
		show_cpu(emu);
	}
}
