#include "terminal.h"
#include <cstdio>
#include <Windows.h>

namespace terminal
{
	bool EnableVTMode()
	{
		// Set output mode to handle virtual terminal sequences
		HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
		if (hOut == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		DWORD dwMode = 0;
		if (!GetConsoleMode(hOut, &dwMode))
		{
			return false;
		}

		dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
		if (!SetConsoleMode(hOut, dwMode))
		{
			return false;
		}
		return true;
	}

	void moveUp(int positions) {
		printf("\x1b[%dA", positions);
	}

	void moveDown(int positions) {
		printf("\x1b[%dB", positions);
	}

	void moveRight(int positions) {
		printf("\x1b[%dC", positions);
	}

	void moveLeft(int positions) {
		printf("\x1b[%dD", positions);
	}

	void moveTo(int row, int col) {
		printf("\x1b[%d;%df", row, col);
	}

	void PrintStatusLine(const char* const pszMessage, COORD const Size)
	{
		printf(CSI "?25l");
		printf(CSI "s");
		moveTo(Size.Y, 1);
		printf(CSI "K");
		printf(pszMessage);
		printf(CSI "u");
	}

	void clearScreen(void) {
		printf("\x1b[%dJ", CLEAR_ALL);
	}

	void clearScreenToBottom(void) {
		printf("\x1b[%dJ", CLEAR_FROM_CURSOR_TO_END);
	}

	void clearScreenToTop(void) {
		printf("\x1b[%dJ", CLEAR_FROM_CURSOR_TO_BEGIN);
	}

	void clearLine(void) {
		printf("\x1b[%dK", CLEAR_ALL);
	}

	void clearLineToRight(void) {
		printf("\x1b[%dK", CLEAR_FROM_CURSOR_TO_END);
	}

	void clearLineToLeft(void) {
		printf("\x1b[%dK", CLEAR_FROM_CURSOR_TO_BEGIN);
	}
}