#include "mos6502.h"

#include <array>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iostream>
#include <map>
#include <readline/readline.h>
#include <readline/history.h>
#include <sstream>
#include <string>

// g++ 6502cli.cpp mos6502.cpp -lreadline -o 6502cli

std::array<uint8_t, 0x10000> memory;
std::map<std::string, std::string> options = {
	{"show-code-stats", "false"},
	{"xa-bin", "xa"},
};

void write_memory(uint16_t addr, uint8_t value) {
	memory[addr] = value;
}

uint8_t read_memory(uint16_t addr) {
	if (addr == 0xffff) {
		throw mos6502::Break();
	}
	return memory[addr];
}

size_t asm_uint(std::string const& s) {
	if (s.empty()) {
		return 0;
	}
	switch (s[0]) {
		case '$':
			return std::stoll(s.substr(1), nullptr, 16);
		case '%':
			return std::stoll(s.substr(1), nullptr, 2);
		default:
			return std::stoll(s);
	};
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
		std::string str_offset;
		std::string str_length;
		line_reader >> str_offset >> str_length;
		size_t offset = asm_uint(str_offset);
		size_t length = asm_uint(str_length);
		std::ostringstream out;
		uint8_t byte_counter = 0;
		for (size_t i = offset; i < offset + length; ++i) {
			out << std::hex << std::setfill('0') << std::setw(2) << uint16_t(memory[i]);
			++byte_counter;
			if (i < offset + length - 1) {
				if (byte_counter == 8) {
					out << "  ";
				}else if (byte_counter == 16) {
					out << '\n';
					byte_counter = 0;
				}else {
					out << ' ';
				}
			}
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
	}else if (opcode == "%options") {
		for (auto const& kv: options) {
			std::cout << kv.first << ": " << kv.second << '\n';
		}
	}else if (opcode == "%set") {
		// Parse arguments
		std::string option;
		std::string value;
		line_reader >> option >> value;

		// Check consistency
		if (options.find(option) == options.end()) {
			std::cerr << "unknown option '" << option << "'\n";
			return true;
		}

		// Set option
		options.at(option) = value;
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
	int ret = ::system((options["xa-bin"] + " /tmp/6502cli.tmp.asm -o /tmp/6502cli.tmp.compiled").c_str());
	if (WIFSIGNALED(ret) && (WTERMSIG(ret) == SIGINT || WTERMSIG(ret) == SIGQUIT)) {
		return false;
	}

	size_t const max_compiled_size = 4096; // Start your scripts with: * = $f000
	std::array<char, max_compiled_size> compiled = {char(0xea)}; // 0xea == NOP

	std::string const compiled_path("/tmp/6502cli.tmp.compiled");
	std::ifstream compiled_reader(compiled_path);
	if (compiled_reader.fail()) {
		std::cerr << "ERROR: failed to open '" << compiled_path << "'\n";
		return false;
	}
	compiled_reader.seekg(0, std::ios_base::end);
	auto const compiled_size = compiled_reader.tellg();
	compiled_reader.seekg(0);
	if (compiled_size > max_compiled_size) {
		std::cerr << "ERROR: compiled code too large: " << compiled_size << " bytes\n";
		return true;
	}
	compiled_reader.read(compiled.data(), max_compiled_size);

	// Run emulator on assembled code
	size_t const compiled_code_offset = memory.size() - compiled.size();
	std::memcpy(memory.data() + compiled_code_offset, compiled.data(), compiled.size());

	uint32_t max_cycles = 1'000'000;
	uint64_t cycles_count = 0;
	emu.pc = compiled_code_offset;
	emu.Run(max_cycles, cycles_count, mos6502::CYCLE_COUNT);

	// Display info about compiled code
	if (options["show-code-stats"] != "false") {
		std::cout << "compiled code size: " << compiled_size << " bytes\n";
		std::cout << "execution (approximate): " << cycles_count << " cycles\n";
	}

	return true;
}

int main(int argc, char** argv) {
	// Parse command line
	if (argc != 1) {
		std::cerr
			<< "usage: " << argv[0] << '\n'
			<< '\n'
			<< "Interactive 6502 executor.\n"
			<< '\n'
			<< "In session, type assembly for it to be executed\n"
			<< "or pseudo opcodes to command the executor:\n"
			<< " %asm <file>: execute a file\n"
			<< " %cpu: display CPU state\n"
			<< " %mem <offset> <length>: display memory region\n"
			<< " %options: display configuration options and their values\n"
			<< " %set <option> <value>: modify a configuration option\n"
			<< '\n'
			<< "Code is compiled, copied at address $f000, then executed.\n"
			<< "Read $ffff to stop execution\n"
			<< " (often happens automatically $fxxx is filled with NOPs after your code.)\n"
		;
		return 1;
	}

	// Check environment
	char* xa_bin = ::getenv("XA_BIN");
	if (xa_bin != NULL) {
		options.at("xa-bin") = xa_bin;
	}

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
