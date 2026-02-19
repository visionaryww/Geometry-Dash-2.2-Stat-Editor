#include <iostream>
#include <string>
#include <stdexcept>
#include <iomanip>

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

int read_int(const std::string& prompt)
{
    int value;
    while (true)
    {
        std::cout << prompt;
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
        std::cout << "\nInput: ";
        std::getline(std::cin, input);

        if (input.size() == 1 && tolower(input[0]) == 'q')
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

        auto stats_instance = game.read<uintptr_t>(game.base + 0x6A4E78);

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
