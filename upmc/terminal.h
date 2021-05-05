#ifndef TERMINAL_H
#define TERMINAL_H

#pragma once
#include <Windows.h>

#define ESC "\x1b"
#define CSI "\x1b["
#define OSC "\x1b]"

namespace terminal
{
	bool EnableVTMode();

	void moveUp(int positions);

	void moveDown(int positions);

	void moveRight(int positions);

	void moveLeft(int positions);

	void moveTo(int row, int col);

	void PrintStatusLine(const char* const pszMessage, COORD const Size);

	void clearScreen(void);

	void clearScreenToBottom(void);

	void clearScreenToTop(void);

	void clearLine(void);

	void clearLineToRight(void);

	void clearLineToLeft(void);

	enum ClearCodes {
		CLEAR_FROM_CURSOR_TO_END,
		CLEAR_FROM_CURSOR_TO_BEGIN,
		CLEAR_ALL
	};
}
#endif