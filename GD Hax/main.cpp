#include <iostream>
#include <string>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <climits>
#include <cctype>

#include "memory.hpp"
#include "GDExploits.hpp"

// use the virtual terminal so we can use ansi escape sequences
static void init_console()
{
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hConsole, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hConsole, dwMode);
}

inline void print_stat_row(int index, const char* stat)
{
    std::cout << " " << std::setw(2) << index << ") " << std::left << std::setw(25) << stat << std::right;
}

static bool possible_user_ptr(uintptr_t address)
{
    return address >= 0x10000 && address < 0x800000000000ull;
}

static std::vector<MemRange> get_scan_ranges(driver& game)
{
    std::vector<MemRange> ranges;
    if (game.get_module_size() == 0)
        return ranges;

    IMAGE_DOS_HEADER dos = game.read<IMAGE_DOS_HEADER>(game.base);
    if (dos.e_magic != IMAGE_DOS_SIGNATURE)
    {
        ranges.push_back({ game.base, game.base + game.get_module_size() });
        return ranges;
    }

    const uintptr_t nt_addr = game.base + static_cast<uintptr_t>(dos.e_lfanew);
    IMAGE_NT_HEADERS64 nt = game.read<IMAGE_NT_HEADERS64>(nt_addr);
    if (nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.NumberOfSections == 0)
    {
        ranges.push_back({ game.base, game.base + game.get_module_size() });
        return ranges;
    }

    uintptr_t section_header_addr = nt_addr + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
    const WORD section_count = nt.FileHeader.NumberOfSections;

    for (WORD i = 0; i < section_count; ++i)
    {
        IMAGE_SECTION_HEADER section = game.read<IMAGE_SECTION_HEADER>(section_header_addr + i * sizeof(IMAGE_SECTION_HEADER));

        const bool is_initialized_data = (section.Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA) != 0;
        const bool is_writable = (section.Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
        if (!is_initialized_data || !is_writable)
            continue;

        const uintptr_t section_start = game.base + section.VirtualAddress;
        const uintptr_t section_size = std::max<uintptr_t>(section.Misc.VirtualSize, section.SizeOfRawData);
        if (section_size == 0)
            continue;

        ranges.push_back({ section_start, section_start + section_size });
    }

    if (ranges.empty())
        ranges.push_back({ game.base, game.base + game.get_module_size() });

    return ranges;
}

static bool validate_stat_info(driver& game, uintptr_t stat_info_addr)
{
    auto stat_info = reinterpret_cast<stat_edits::StatInfo*>(stat_info_addr);
    uintptr_t list_end = game.read<uintptr_t>(&stat_info->list_end);
    uintptr_t stat_table = game.read<uintptr_t>(&stat_info->stat_table);
    uint64_t list_length = game.read<uint64_t>(&stat_info->list_length);
    uint64_t hash_mask = game.read<uint64_t>(&stat_info->hash_mask);

    if (!possible_user_ptr(list_end) || !possible_user_ptr(stat_table))
        return false;

    if (list_length == 0 || list_length > 100000)
        return false;

    if (hash_mask == 0 || hash_mask > 0xFFFFF || (hash_mask & (hash_mask + 1)) != 0)
        return false;

    auto jumps = stat_edits::get_stat_addr(game, stat_info, stat_edits::StatType::JUMPS);
    if (jumps == nullptr || game.read<stat_edits::StatType>(&jumps->stat) != stat_edits::StatType::JUMPS)
        return false;

    auto attempts = stat_edits::get_stat_addr(game, stat_info, stat_edits::StatType::ATTEMPTS);
    if (attempts == nullptr || game.read<stat_edits::StatType>(&attempts->stat) != stat_edits::StatType::ATTEMPTS)
        return false;

    return true;
}

static bool detect_stats_instance_offset(driver& game, uintptr_t& out_offset)
{
    const auto ranges = get_scan_ranges(game);

    for (const auto& range : ranges)
    {
        uintptr_t start = (range.start + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1);

        for (uintptr_t address = start; address + sizeof(uintptr_t) <= range.end; address += sizeof(uintptr_t))
        {
            const uintptr_t instance = game.read<uintptr_t>(address);
            if (!possible_user_ptr(instance))
                continue;

            if (!validate_stat_info(game, instance + 0x248))
                continue;

            if (!validate_stat_info(game, instance + 0x288))
                continue;

            out_offset = address - game.base;
            return true;
        }
    }

    return false;
}

int read_int(const std::string& prompt)
{
    int value;
    while (true)
    {
        std::cout << prompt << std::flush;
        std::string input;
        std::getline(std::cin, input);

        try
        {
            value = std::stoi(input);
            return value;
        }
        catch (const std::invalid_argument&)
        {
            std::cout << "Invalid input. Please enter an integer.\n";
        }
        catch (const std::out_of_range&)
        {
            std::cout << "Number is out of range. Please enter a valid integer.\n";
        }
    }
}

stat_edits::StatType try_parse_input()
{
    std::string input;
    while (true)
    {
        std::cout << "\nInput: " << std::flush;
        std::getline(std::cin, input);

        if (input.size() == 1 && std::tolower(static_cast<unsigned char>(input[0])) == 'q')
        {
            return stat_edits::StatType::NONE;
        }

        try
        {
            size_t pos;
            int value = std::stoi(input, &pos);

            // Check if entire string was consumed
            if (pos == input.length())
            {
                stat_edits::StatType result = stat_edits::StatType(value);
                if (stat_edits::StatType::NONE < result && result <= stat_edits::StatType::TOTAL_COUNT)
                    return result;

                std::cout << CLEAR_SCREEN "Invalid input. Input a value that corresponds to a stat.\n";
            }
            else
            {
                std::cout << CLEAR_SCREEN "Invalid input. Please enter only an integer.\n";
            }
        }
        catch (const std::invalid_argument&)
        {
            std::cout << CLEAR_SCREEN "Invalid input. Please enter an integer.\n";
        }
        catch (const std::out_of_range&)
        {
            std::cout << CLEAR_SCREEN "Number is out of range. Please enter a valid integer.\n";
        }

        return stat_edits::StatType::INVALID;
    }
}

int main()
{
    const int total_options = stat_edits::StatType::TOTAL_COUNT;

    auto game = driver(L"GeometryDash.exe");
    if (!game.is_attached())
    {
        std::cout << "Failed to find game process!\n";
        PAUSE_EXIT();
        return EXIT_FAILURE;
    }

    uintptr_t stats_instance_offset = 0;
    if (!detect_stats_instance_offset(game, stats_instance_offset))
    {
        std::cout << "Failed to auto-detect the stats offset for this game version!\n";
        PAUSE_EXIT();
        return EXIT_FAILURE;
    }

    std::cout << "Detected stats offset: 0x" << std::hex << stats_instance_offset << std::dec << "\n";

    while (true)
    {
        stat_edits::StatType input = stat_edits::StatType::INVALID;
        while (input < 0 || total_options < input)
        {
            std::cout << "Input 'q' to exit\nSelect a stat to modify [1 - " << total_options << "]:" << "\n";

            for (int i = 1; i <= total_options; ++i)
            {
                print_stat_row(i, stat_edits::stat_types[i - 1]);

                if (i % 2 == 0)
                    std::cout << "\n";
            }

            input = try_parse_input();
            if (input == stat_edits::StatType::NONE)
            {
                std::cout << "Exiting...\n";
                return EXIT_SUCCESS;
            }
        }

        std::cout << "\nInput value to set for stat '" << stat_edits::stat_types[input - 1] << "'!\n";
        int value = read_int("Value [" + std::to_string(INT_MIN) + " - " + std::to_string(INT_MAX) + "]: ");
        std::cout << "Setting '" << stat_edits::stat_types[input - 1] << "' value to " << value << "!\n";

        // since those random achievements aren't in the list anymore, adjust to fix gaunlets and list rewards since they come after
        if (input >= 30)
            input = stat_edits::StatType(input + 10);

        auto stats_instance = game.read<uintptr_t>(game.base + stats_instance_offset);

        auto stat_info = reinterpret_cast<stat_edits::StatInfo*>(stats_instance + 0x248);
        auto stat_info_delta = reinterpret_cast<stat_edits::StatInfo*>(stats_instance + 0x288);

        stat_edits::StatLinkedList* stat_info_delta_addr = stat_edits::get_stat_addr(game, stat_info_delta, input);
        if (stat_info_delta_addr == nullptr)
        {
            std::cout << "Failed to get the delta address for the specified stat type!\n";
            PAUSE_EXIT();
            return EXIT_FAILURE;
        }

        auto delta = game.read<uint32_t>(&stat_info_delta_addr->value);

        stat_edits::StatLinkedList* stat_info_addr = stat_edits::get_stat_addr(game, stat_info, input);
        if (stat_info_addr == nullptr)
        {
            std::cout << "Failed to get the stat value address for the specified stat type!\n";
            PAUSE_EXIT();
            return EXIT_FAILURE;
        }

        game.write<uint32_t>(&stat_info_addr->value, value + delta);

        std::cout << "Success! Press any key to continue!\n";
        PAUSE();
        std::cout << CLEAR_SCREEN;
    }

    return EXIT_SUCCESS;
}
