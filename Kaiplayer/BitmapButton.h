//  Copyright (c) 2016 - 2020, Marcin Drob

//  Kainote is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.

//  Kainote is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.

//  You should have received a copy of the GNU General Public License
//  along with Kainote.  If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include <wx/string.h>
#include <wx/statbmp.h>


class BitmapButton : public wxStaticBitmap
{
public:
	BitmapButton(wxWindow* parent, wxBitmap bitmap,wxBitmap bitmap1, int hotkeyId, const wxString &tooltip, const wxPoint& pos, const wxSize& size, int window = 3/*VIDEO_HOTKEY*/);
	virtual ~BitmapButton();
	void ChangeBitmap(bool play);

private:
	wxBitmap bmp;
	wxBitmap bmp1;
	wxImage img;
	bool enter;
	void OnLeftDown(wxMouseEvent& event);
	void SetToolTip(const wxString &_toolTip = L"");
	wxString name;
	int window;
	int hotkeyId;
};

