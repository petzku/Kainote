﻿//  Copyright (c) 2012 - 2020, Marcin Drob

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

#include "SubsGridWindow.h"
#include "config.h"
//#include "Utils.h"
#include "EditBox.h"

#include "kainoteMain.h"
#include "kaiMessageBox.h"
#include "SubsGridFiltering.h"
#include "SubsGridPreview.h"
#include <wx/regex.h>
#include "GraphicsD2D.h"
#include "BidiConversion.h"


SubsGridWindow::SubsGridWindow(wxWindow *parent, const long int id, const wxPoint& pos, const wxSize& size, long style)
	:SubsGridBase(parent, id, pos, size, style)
{
	visibleColumns = Options.GetInt(GRID_HIDE_COLUMNS);
	hideOverrideTags = Options.GetBool(GRID_HIDE_TAGS);
	bmp = NULL;
	SetStyle();
	AdjustWidths();
	//SetFocus();
	Bind(wxEVT_PAINT, &SubsGridWindow::OnPaint, this);
	Bind(wxEVT_SIZE, &SubsGridWindow::OnSize, this);
	Bind(wxEVT_KEY_DOWN, &SubsGridWindow::OnKeyPress, this);
	Bind(wxEVT_TIMER, &SubsGridWindow::OnBackupTimer, this, ID_AUTIMER);
	Bind(wxEVT_ERASE_BACKGROUND, [=](wxEraseEvent &evt){});
	Bind(wxEVT_MOUSE_CAPTURE_LOST, &SubsGridWindow::OnLostCapture, this);
	Bind(wxEVT_SCROLLWIN_THUMBTRACK, &SubsGridWindow::OnScroll, this);
	Bind(wxEVT_MOUSEWHEEL, &SubsGridWindow::OnMouseEvent, this);
	Bind(wxEVT_MOTION, &SubsGridWindow::OnMouseEvent, this);
	Bind(wxEVT_LEFT_DOWN, &SubsGridWindow::OnMouseEvent, this);
	Bind(wxEVT_LEFT_UP, &SubsGridWindow::OnMouseEvent, this);
	Bind(wxEVT_LEFT_DCLICK, &SubsGridWindow::OnMouseEvent, this);
	Bind(wxEVT_MIDDLE_DOWN, &SubsGridWindow::OnMouseEvent, this);
	Bind(wxEVT_RIGHT_DOWN, &SubsGridWindow::OnMouseEvent, this);
	Bind(wxEVT_RIGHT_UP, &SubsGridWindow::OnMouseEvent, this);
	Bind(wxEVT_SET_FOCUS, [=](wxFocusEvent& evt) {Refresh(false); });
	Bind(wxEVT_KILL_FOCUS, [=](wxFocusEvent& evt) {Refresh(false); });
}

SubsGridWindow::~SubsGridWindow()
{
	if (bmp){ delete bmp; bmp = NULL; }
	if (preview){ preview->DestroyPreview(false, true); preview = NULL; }
	if (thisPreview){ thisPreview->DestroyPreview(); thisPreview = NULL; }
}

void SubsGridWindow::SetStyle()
{
	const wxString & fontname = Options.GetString(GRID_FONT);
	font.SetFaceName(fontname);
	if (!font.IsOk())
		font.SetFamily(wxFONTFAMILY_SWISS);
	font.SetWeight(wxFONTWEIGHT_NORMAL);
	wxFont* defaultFont = Options.GetFont(Options.GetInt(GRID_FONT_SIZE) - 10);
	font.SetPixelSize(defaultFont->GetPixelSize());
	int fw, fh;
	GetTextExtent(L"#TWFfGH", &fw, &fh, NULL, NULL, &font);
	GridHeight = ((fh + 3) * 2) / 2;
	Refresh(false);
}


void SubsGridWindow::OnPaint(wxPaintEvent& event)
{
	int w = 0;
	int h = 0;
	GetClientSize(&w, &h);
	int firstCol = GridWidth[0] + 1;
	wxRegionIterator upd(GetUpdateRegion());
	while (upd) {
		wxRect rect(upd.GetRect());
		upd++;
		//draw bitmap fragment, only use for
		//Windows repaint draw existed bitmap
		//instead of making a new
		//when window size changed don't use part redraw
		if ((rect.width < w || rect.height < h) && bmp && lastWidth == w && lastHeight == h) {
			wxMemoryDC tdc;
			tdc.SelectObject(*bmp);
			wxPaintDC dc(this);
			int firstColPosX = firstCol + posX;
			if (rect.x <= firstCol + posX)
				dc.Blit(rect.x, rect.y, firstColPosX > rect.width ? rect.width : firstColPosX, rect.height, &tdc, rect.x, rect.y);
			if (rect.x + rect.width > firstColPosX) {
				int blitX = firstColPosX > rect.x ? firstColPosX : rect.x;
				int blitWidth = firstColPosX > rect.x ? rect.width - (firstColPosX - rect.x) : rect.width;
				dc.Blit(blitX, rect.y, blitWidth + scHor, rect.height, &tdc, scHor + blitX, rect.y);
			}
			if (!upd) {
				return;
			}
		}
	}
	bool bg = false;
	int size = file->GetIdCount();
	wxPoint previewpos;
	wxSize previewsize;
	if (preview){
		previewpos = preview->GetPosition();
		previewsize = preview->GetSize();
		size += (previewsize.y / (GridHeight + 1)) + 1;
	}
	panelrows = (h / (GridHeight + 1)) + 1;
	if (scrollPosition < 0){ scrollPosition = 0; scrollPositionId = 0; }
	int scrows = scrollPositionId + panelrows;
	//get to end of subtitles
	if (scrows >= size + 3){
		bg = true;
		scrows = size + 1;
		scrollPositionId = (scrows - panelrows) + 2;// end of subtitles
		scrollPosition = file->GetElementById(scrollPositionId);
		// when all subtitles are visible do not scrolling position = 0
		if (panelrows > size + 3){ scrollPosition = 0; scrollPositionId = 0; }
	}
	else if (scrows >= size + 2) {
		bg = true;
		scrows--;
		//reduced to avoid crash or maybe now not needed cause is key + i < getcount()
	}
	
	if (SetScrollBar(wxVERTICAL, scrollPositionId, panelrows, size + 3, panelrows - 3)){
		GetClientSize(&w, &h);
	}

	lastWidth = w;
	lastHeight = h;

	// Prepare bitmap
	if (bmp) {
		if (bmp->GetWidth() < w + scHor || bmp->GetHeight() < h) {
			delete bmp;
			bmp = NULL;
		}
	}
	if (!bmp) bmp = new wxBitmap(w + scHor, h);

	// Draw bitmap
	wxMemoryDC tdc;
	tdc.SelectObject(*bmp);

	
	GraphicsRenderer *renderer = GraphicsRenderer::GetDirect2DRenderer();
	GraphicsContext* gc = renderer? renderer->CreateContext(tdc) : NULL;
	if (gc)
		PaintD2D(gc, w, h, size, scrows, previewpos, previewsize, bg);
	else
	{

		const wxColour &header = Options.GetColour(GRID_HEADER);
		const wxColour &headerText = Options.GetColour(GRID_HEADER_TEXT);
		const wxColour &labelBkCol = Options.GetColour(GRID_LABEL_SAVED);
		const wxColour &labelBkColN = Options.GetColour(GRID_LABEL_NORMAL);
		const wxColour &labelBkColM = Options.GetColour(GRID_LABEL_MODIFIED);
		const wxColour &labelBkColD = Options.GetColour(GRID_LABEL_DOUBTFUL);
		const wxColour &linesCol = Options.GetColour(GRID_LINES);
		const wxColour &subsBkCol = Options.GetColour(GRID_DIALOGUE);
		const wxColour &comm = Options.GetColour(GRID_COMMENT);
		const wxColour &seldial = Options.GetColour(GRID_SELECTION);
		const wxColour &textcol = Options.GetColour(GRID_TEXT);
		const wxColour &collcol = Options.GetColour(GRID_COLLISIONS);
		const wxColour &SpelcheckerCol = Options.GetColour(GRID_SPELLCHECKER);
		const wxColour &ComparisonCol = Options.GetColour(GRID_COMPARISON_OUTLINE);
		const wxColour &ComparisonBG = Options.GetColour(GRID_COMPARISON_BACKGROUND_NOT_MATCH);
		const wxColour &ComparisonBGMatch = Options.GetColour(GRID_COMPARISON_BACKGROUND_MATCH);
		const wxColour &ComparisonBGCmnt = Options.GetColour(GRID_COMPARISON_COMMENT_BACKGROUND_NOT_MATCH);
		const wxColour &ComparisonBGCmntMatch = Options.GetColour(GRID_COMPARISON_COMMENT_BACKGROUND_MATCH);
		const wxString &chtag = Options.GetString(GRID_TAGS_SWAP_CHARACTER);
		int chtagLen = chtag.length();
		const wxColour &visibleOnVideo = Options.GetColour(GRID_LINE_VISIBLE_ON_VIDEO);
		bool SpellCheckerOn = Options.GetBool(SPELLCHECKER_ON);

		tdc.SetFont(font);
		tdc.SetPen(*wxTRANSPARENT_PEN);
		tdc.SetBrush(wxBrush(linesCol));
		tdc.DrawRectangle(0, 0, w + scHor, h);

		int ilcol;
		posY = 0;

		bool isComment = false;
		bool unknownStyle = false;
		bool shorttime = false;
		bool startBlock = false;
		bool badWraps = false;
		int states = 0;
		int startDrawPosYFromPlus = 0;
		size_t KeySize = GetCount();

		if (SpellErrors.size() < KeySize){
			SpellErrors.resize(KeySize);
		}

		Dialogue *acdial = GetDialogue(MID(0, currentLine, size - 1));
		Dialogue *Dial = NULL;
		int VideoPos = tab->Video->GetState() != None ? tab->Video->Tell() : -1;

		int fw, fh, bfw, bfh;
		wxColour kol;
		visibleLines.clear();

		std::vector<wxString> strings;
		//refresh have to be fast, reduce recalculation id to key to minimum
		//scrollPositionId it's also strored 
		int key = scrollPosition - 1;
		int id = scrollPositionId - 1;
		int idmarkerPos = -1;
		int idcurrentLine = -1;

		while (key + 1 <= KeySize && id < scrows - 1){
			bool isHeadline = (key < scrollPosition);
			if (!isHeadline){
				Dial = file->GetDialogue(key);
				if (!Dial->isVisible){ key++; continue; }
			}
			bool comparison = false;
			bool isSelected = false;
			strings.clear();
			wxString convertedText;
			bool isRTL = false;
			
			if (isHeadline){
				strings.push_back(L"#");
				if (subsFormat < SRT){
					strings.push_back(_("W."));
				}
				strings.push_back(_("Start"));
				if (subsFormat != TMP){
					strings.push_back(_("Koniec"));
				}
				if (subsFormat < SRT){
					strings.push_back(_("Styl"));
					strings.push_back(_("Aktor"));
					strings.push_back(_("M.L."));
					strings.push_back(_("M.P."));
					strings.push_back(_("M.Pi."));
					strings.push_back(_("Efekt"));
				}
				if (subsFormat != TMP){ strings.push_back(_("ZNS")); }
				strings.push_back(_("Łamania"));
				strings.push_back(showOriginal ? _("Tekst oryginalny") : _("Tekst"));
				if (showOriginal){ strings.push_back(_("Tekst tłumaczenia")); }
				kol = header;
			}
			else{
				TextData &Misspells = SpellErrors[key];
				strings.push_back(wxString::Format(L"%i", id + 1));

				isComment = Dial->IsComment;
				if (key == markedLine)
					idmarkerPos = id;
				if (key == currentLine)
					idcurrentLine = id;

				states = Dial->GetState();
				if (subsFormat < SRT){
					strings.push_back(wxString::Format(L"%i", Dial->Layer));
				}
				
				if (showFrames && tab->Video->HasFFMS2()){
					Provider *FFMS2 = tab->Video->GetFFMS2();
					wxString frame;
					frame << FFMS2->GetFramefromMS(Dial->Start.mstime);
					strings.push_back(frame);
					if (subsFormat != TMP){
						frame = L"";
						frame << FFMS2->GetFramefromMS(Dial->End.mstime) - 1;
						strings.push_back(frame);
					}
				}
				else{
					strings.push_back(Dial->Start.raw(subsFormat));
					if (subsFormat != TMP){ strings.push_back(Dial->End.raw(subsFormat)); }
				}

				if (subsFormat < SRT){
					if (FindStyle(Dial->Style) == -1){ unknownStyle = true; }
					else{ unknownStyle = false; }
					strings.push_back(Dial->Style);
					strings.push_back(Dial->Actor);
					strings.push_back(wxString::Format(L"%i", Dial->MarginL));
					strings.push_back(wxString::Format(L"%i", Dial->MarginR));
					strings.push_back(wxString::Format(L"%i", Dial->MarginV));
					strings.push_back(Dial->Effect);
				}
				wxString txt = Dial->Text;
				wxString txttl = Dial->TextTl;
				bool isTl = (hasTLMode && txttl != L"");
				wxString& checkingText = (isTl) ? txttl : txt;
				if (CheckRTL(&checkingText)) {
					ConvertToRTLCharsSpellchecker(&checkingText, &convertedText);
					isRTL = true;
				}
				if (!isComment) {
					//here are generated misspells table, chars table, and wraps;
					//on original do not use spellchecking only calculating wraps and cps;
					bool originalInTLMode = hasTLMode && txttl == L"";
					Misspells.Init(isRTL ? convertedText : checkingText, SpellCheckerOn && !originalInTLMode,
						subsFormat, hideOverrideTags ? chtagLen : -1);
				}
				if (!isComment && subsFormat != TMP && !(CPS & visibleColumns)) {
					int chtime = Misspells.GetCPS(Dial);
					strings.push_back(wxString::Format(L"%i", chtime));
					shorttime = chtime > 15;
				}
				else if (subsFormat != TMP) {
					strings.push_back(L"");
					shorttime = false;
				}
				if (!isComment && !(WRAPS & visibleColumns)) {
					strings.push_back(Misspells.GetStrippedWraps());
					badWraps = Misspells.badWraps;
				}
				else {
					strings.push_back(L"");
					badWraps = false;
				}
				if (hideOverrideTags) {
					wxRegEx reg(subsFormat == SRT? L"\\<[^\\<]*\\>" : L"\\{[^\\{]*\\}", wxRE_ADVANCED);
					if (!showOriginal && !isTl)
						reg.ReplaceAll(&txt, chtag);
					if ((!showOriginal && isTl) || showOriginal)
						reg.ReplaceAll(&txttl, chtag);
				}

				if (txt.length() > 1000){ txt = txt.SubString(0, 1000) + L"..."; }
				if (txttl.length() > 1000){ txttl = txttl.SubString(0, 1000) + L"..."; }
				strings.push_back((!showOriginal && isTl) ? txttl : txt);
				if (showOriginal){ strings.push_back(txttl); }

				isSelected = file->IsSelected(key);
				comparison = (Comparison && Comparison->at(key).size() > 0);
				bool comparisonMatch = (Comparison && !Comparison->at(key).differences);
				bool visibleLine = (Dial->Start.mstime <= VideoPos && Dial->End.mstime > VideoPos);
				kol = (comparison) ? ComparisonBG :
					(comparisonMatch) ? ComparisonBGMatch :
					(visibleLine) ? visibleOnVideo :
					subsBkCol;
				if (isComment){ kol = (comparison) ? ComparisonBGCmnt : (comparisonMatch) ? ComparisonBGCmntMatch : comm; }
				if (isSelected){
					kol = GetColorWithAlpha(seldial, kol);
				}
				visibleLines.push_back(visibleLine);
			}

			if (isFiltered){
				posX = 11;
				unsigned char hasHiddenBlock = file->CheckIfHasHiddenBlock(key, isHeadline);
				if (hasHiddenBlock){
					tdc.SetBrush(*wxTRANSPARENT_BRUSH);
					tdc.SetPen(textcol);
					int halfGridHeight = (GridHeight / 2);
					int newPosY = posY + GridHeight + 1;
					int startDrawPosY = newPosY + ((GridHeight - 10) / 2) - halfGridHeight;
					tdc.DrawRectangle(1, startDrawPosY, 9, 9);
					tdc.DrawLine(3, newPosY - 1, 8, newPosY - 1);
					if (hasHiddenBlock == 1){
						tdc.DrawLine(5, startDrawPosY + 2, 5, startDrawPosY + 7);
					}
					//tdc.SetPen(SpelcheckerCol);
					tdc.DrawLine(10, newPosY - 1, w + scHor, newPosY - 1);
				}
				if (Dial){
					if (!startBlock && Dial->isVisible == VISIBLE_BLOCK){
						startDrawPosYFromPlus = posY + 4; startBlock = true;
					}
					bool isLastLine = (id >= scrows - 2);
					bool notVisibleBlock = Dial->isVisible != VISIBLE_BLOCK;
					if (startBlock && (notVisibleBlock || isLastLine)){
						tdc.SetBrush(*wxTRANSPARENT_BRUSH);
						tdc.SetPen(textcol);
						int halfLine = posY - 1;
						if (isLastLine && !notVisibleBlock){ halfLine = posY + GridHeight; }
						tdc.DrawLine(5, startDrawPosYFromPlus, 5, halfLine);
						tdc.DrawLine(5, halfLine, w + scHor, halfLine);
						startBlock = false;
					}
				}


			}
			else{
				posX = 0;
			}

			ilcol = strings.size();


			wxRect cur;
			bool isCenter;
			wxColour label = (states == 0) ? labelBkColN : (states == 2) ? labelBkCol :
				(states == 1) ? labelBkColM : labelBkColD;
			for (int j = 0; j < ilcol; j++){
				if (showOriginal && j == ilcol - 2){
					int podz = (w + scHor - posX) / 2;
					GridWidth[j] = podz;
					GridWidth[j + 1] = podz;
				}

				if (!showOriginal&&j == ilcol - 1){ GridWidth[j] = w + scHor - posX; }


				if (GridWidth[j] < 1){
					continue;
				}
				tdc.SetPen(*wxTRANSPARENT_PEN);

				tdc.SetBrush(wxBrush((j == 0 && !isHeadline) ? label : kol));
				if (unknownStyle && j == 4 ||
					shorttime && (j == 10 || (j == 3 && subsFormat != ASS && subsFormat != TMP)) ||
					badWraps && (j == 11 || (j == 4 && subsFormat > ASS && subsFormat != TMP) || (j == 2 && subsFormat == TMP))) {
					tdc.SetBrush(wxBrush(SpelcheckerCol));
				}

				tdc.DrawRectangle(posX, posY, GridWidth[j], GridHeight);

				if (!isHeadline && j == ilcol - 1){
					wxString& text = (isRTL) ? convertedText : strings[j];
					SpellErrors[key].DrawMisspells(text, wxPoint(posX, posY), this, &tdc, SpelcheckerCol, GridHeight, font);

					if (comparison){
						tdc.SetTextForeground(ComparisonCol);
						for (size_t c = 1; c < Comparison->at(key).size(); c += 2){
							wxString cmp = text.SubString(Comparison->at(key)[c], Comparison->at(key)[c + 1]);

							if (cmp == L""){ continue; }
							if (cmp == L" "){ cmp = L"_"; }
							wxString bcmp;
							if (Comparison->at(key)[c]>0){
								bcmp = text.Mid(0, Comparison->at(key)[c]);
								GetTextExtent(bcmp, &bfw, &bfh, NULL, NULL, &font);
							}
							else{ bfw = 0; }

							GetTextExtent(cmp, &fw, &fh, NULL, NULL, &font);

							tdc.DrawText(cmp, posX + bfw + 2, posY);
							tdc.DrawText(cmp, posX + bfw + 4, posY);
							tdc.DrawText(cmp, posX + bfw + 2, posY + 2);
							tdc.DrawText(cmp, posX + bfw + 4, posY + 2);
						}

					}

				}


				bool collis = (!isHeadline && acdial && key != currentLine &&
					(Dial->Start < acdial->End && Dial->End > acdial->Start));

				if (subsFormat < SRT) { isCenter = !(j == 4 || j == 5 || j == 9 || j > 10); }
				else if (subsFormat == TMP) { isCenter = (j == 0 || j == 1); }
				else { isCenter = !(j == 4 || j == 5); }

				tdc.SetTextForeground((isHeadline) ? headerText : (collis) ? collcol : textcol);

				int treeState = (Dial) ? Dial->treeState : 0;
				if (j > 0 && treeState == TREE_DESCRIPTION){
					tdc.SetBrush(comm);
					tdc.SetPen(*wxTRANSPARENT_PEN);
					tdc.DrawRectangle(posX + 1, posY, w - 1, GridHeight);
					// GetDialogueKey was made for loops no checks
					Dialogue *nextDial = (key < file->GetCount() - 1) ? file->GetDialogue(key + 1) : NULL;
					if (nextDial && nextDial->treeState == TREE_CLOSED)
						tdc.DrawBitmap(wxBITMAP_PNG(L"arrow_list"), posX + 6, posY + 5);
					else{
						wxBitmap bmp(wxBITMAP_PNG(L"arrow_list"));
						wxImage img = bmp.ConvertToImage();
						img = img.Rotate180();
						tdc.DrawBitmap(img, posX + 6, posY + 5);
					}
					tdc.DrawText(Dial->Text, posX + 23, posY + 1);
					break;
				}
				cur = wxRect(posX + 3, posY, GridWidth[j] - 6, GridHeight);
				tdc.SetClippingRegion(cur);
				tdc.DrawLabel(strings[j], cur, isCenter ? wxALIGN_CENTER : (wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT));
				tdc.DestroyClippingRegion();

				posX += GridWidth[j] + 1;


			}

			posY += GridHeight + 1;
			if (preview){
				if (posY >= previewpos.y - 2 && posY < previewpos.y + previewsize.y){
					posY = previewpos.y + previewsize.y + 3;
				}
				else if (posY > h){ scrows = id + 1; break; }
			}
			id++;
			key++;
		}

		posX = (isFiltered) ? 11 : 0;
		if (bg){
			tdc.SetPen(*wxTRANSPARENT_PEN);
			tdc.SetBrush(wxBrush(Options.GetColour(GRID_BACKGROUND)));
			tdc.DrawRectangle(posX, posY, w + scHor, h);
		}
		if (size > 0){
			if (idmarkerPos != -1){
				tdc.SetBrush(*wxTRANSPARENT_BRUSH);
				tdc.SetPen(wxPen(Options.GetColour(GRID_ACTIVE_LINE), 3));
				int ypos = ((idmarkerPos - scrollPositionId + 1) * (GridHeight + 1));
				if (preview && ypos >= previewpos.y - 2){ ypos += previewsize.y + 5; }
				tdc.DrawRectangle(posX + 1, ypos - 1, (GridWidth[0] - 1), GridHeight + 2);
			}

			if (idcurrentLine != -1){
				tdc.SetBrush(*wxTRANSPARENT_BRUSH);
				tdc.SetPen(wxPen(Options.GetColour(GRID_ACTIVE_LINE)));
				int ypos = ((idcurrentLine - scrollPositionId + 1) * (GridHeight + 1));
				if (preview && ypos >= previewpos.y - 2){ ypos += previewsize.y + 5; }
				tdc.DrawRectangle(posX, ypos - 1, w + scHor - posX, GridHeight + 2);
			}
		}
		if (HasFocus()) {
			tdc.SetBrush(*wxTRANSPARENT_BRUSH);
			tdc.SetPen(wxPen(Options.GetColour(EDITOR_BORDER_ON_FOCUS)));
			tdc.DrawRectangle(0, 0, w + scHor - 1, h - 1);
		}
	}
	wxPaintDC dc(this);
	dc.Blit(0, 0, firstCol + posX, h, &tdc, 0, 0);
	dc.Blit(firstCol + posX, 0, w + scHor, h, &tdc, scHor + firstCol + posX, 0);
}

void SubsGridWindow::PaintD2D(GraphicsContext *gc, int w, int h, int size, int scrows, wxPoint previewpos, wxSize previewsize, bool bg)
{
	
	const wxColour &header = Options.GetColour(GRID_HEADER);
	const wxColour &headerText = Options.GetColour(GRID_HEADER_TEXT);
	const wxColour &labelBkCol = Options.GetColour(GRID_LABEL_SAVED);
	const wxColour &labelBkColN = Options.GetColour(GRID_LABEL_NORMAL);
	const wxColour &labelBkColM = Options.GetColour(GRID_LABEL_MODIFIED);
	const wxColour &labelBkColD = Options.GetColour(GRID_LABEL_DOUBTFUL);
	const wxColour &linesCol = Options.GetColour(GRID_LINES);
	const wxColour &subsBkCol = Options.GetColour(GRID_DIALOGUE);
	const wxColour &comment = Options.GetColour(GRID_COMMENT);
	const wxColour &seldial = Options.GetColour(GRID_SELECTION);
	const wxColour &textcol = Options.GetColour(GRID_TEXT);
	const wxColour &collcol = Options.GetColour(GRID_COLLISIONS);
	const wxColour &SpelcheckerCol = Options.GetColour(GRID_SPELLCHECKER);
	const wxColour &ComparisonCol = Options.GetColour(GRID_COMPARISON_OUTLINE);
	const wxColour &ComparisonBG = Options.GetColour(GRID_COMPARISON_BACKGROUND_NOT_MATCH);
	const wxColour &ComparisonBGMatch = Options.GetColour(GRID_COMPARISON_BACKGROUND_MATCH);
	const wxColour &ComparisonBGCmnt = Options.GetColour(GRID_COMPARISON_COMMENT_BACKGROUND_NOT_MATCH);
	const wxColour &ComparisonBGCmntMatch = Options.GetColour(GRID_COMPARISON_COMMENT_BACKGROUND_MATCH);
	const wxString &chtag = Options.GetString(GRID_TAGS_SWAP_CHARACTER);
	int chtagLen = chtag.length();
	const wxColour &visibleOnVideo = Options.GetColour(GRID_LINE_VISIBLE_ON_VIDEO);
	bool SpellCheckerOn = Options.GetBool(SPELLCHECKER_ON);

	//gc->SetFont(font);
	gc->SetPen(*wxTRANSPARENT_PEN);
	gc->SetBrush(wxBrush(linesCol));
	gc->DrawRectangle(0, 0, w + scHor, h);

	int numColumns;
	posY = 0;

	bool isComment = false;
	bool unknownStyle = false;
	bool shorttime = false;
	bool startBlock = false;
	bool badWraps = false;
	int states = 0;
	int startDrawPosYFromPlus = 0;
	size_t KeySize = GetCount();

	if (SpellErrors.size() < KeySize){
		SpellErrors.resize(KeySize);
	}

	Dialogue *acdial = GetDialogue(MID(0, currentLine, size - 1));
	Dialogue *Dial = NULL;
	int VideoPos = tab->Video->GetState() != None ? tab->Video->Tell() : -1;

	double fw, fh, bfw, bfh;
	wxColour col;
	visibleLines.clear();

	std::vector<wxString> strings;
	//refresh have to be fast, reduce recalculation id to key to minimum
	//scrollPositionId it's also strored 
	int key = scrollPosition - 1;
	int id = scrollPositionId - 1;
	int idmarkerPos = -1;
	int idcurrentLine = -1;
	float fontApproxSize = ((float)font.GetPixelSize().GetHeight()) / 2.5f;
	int maxTextLength = 100;

	while (key + 1 <= KeySize && id < scrows - 1){
		bool isHeadline = (key < scrollPosition);
		if (!isHeadline){
			Dial = file->GetDialogue(key);
			if (!Dial->isVisible){ key++; continue; }
		}
		bool comparison = false;
		bool isSelected = false;
		strings.clear();
		wxString convertedText;
		bool isRTL = false;

		if (isHeadline){
			strings.push_back(L"#");
			if (subsFormat < SRT){
				strings.push_back(_("W."));
			}
			strings.push_back(_("Start"));
			if (subsFormat != TMP){
				strings.push_back(_("Koniec"));
			}
			if (subsFormat < SRT){
				strings.push_back(_("Styl"));
				strings.push_back(_("Aktor"));
				strings.push_back(_("M.L."));
				strings.push_back(_("M.P."));
				strings.push_back(_("M.Pi."));
				strings.push_back(_("Efekt"));
			}
			if (subsFormat != TMP) {
				strings.push_back(_("ZNS"));
			}
			strings.push_back(_("Łamania"));
			strings.push_back(showOriginal ? _("Tekst oryginalny") : _("Tekst"));
			if (showOriginal){ strings.push_back(_("Tekst tłumaczenia")); }
			col = header;
		}
		else{
			TextData &Misspells = SpellErrors[key];
			strings.push_back(wxString::Format(L"%i", id + 1));

			isComment = Dial->IsComment;
			if (key == markedLine)
				idmarkerPos = id;
			if (key == currentLine)
				idcurrentLine = id;

			states = Dial->GetState();
			if (subsFormat < SRT){
				strings.push_back(wxString::Format(L"%i", Dial->Layer));
			}
			if (showFrames && tab->Video->HasFFMS2()){
				Provider *FFMS2 = tab->Video->GetFFMS2();
				wxString frame;
				frame << FFMS2->GetFramefromMS(Dial->Start.mstime);
				strings.push_back(frame);
				if (subsFormat != TMP){
					frame = L"";
					frame << FFMS2->GetFramefromMS(Dial->End.mstime) - 1;
					strings.push_back(frame);
				}
			}
			else{
				strings.push_back(Dial->Start.raw(subsFormat));
				if (subsFormat != TMP){ strings.push_back(Dial->End.raw(subsFormat)); }
			}

			if (subsFormat < SRT){
				if (FindStyle(Dial->Style) == -1){ unknownStyle = true; }
				else{ unknownStyle = false; }
				strings.push_back(Dial->Style);
				strings.push_back(Dial->Actor);
				strings.push_back(wxString::Format(L"%i", Dial->MarginL));
				strings.push_back(wxString::Format(L"%i", Dial->MarginR));
				strings.push_back(wxString::Format(L"%i", Dial->MarginV));
				strings.push_back(Dial->Effect);
			}

			wxString txt = Dial->Text;
			wxString txttl = Dial->TextTl;
			bool isTl = (hasTLMode && txttl != L"");
			wxString& checkingText = (isTl) ? txttl : txt;
			if (CheckRTL(&checkingText)) {
				ConvertToRTLCharsSpellchecker(&checkingText, &convertedText);
				isRTL = true;
			}
			
			if (!isComment) {
				//here are generated misspells table, chars table, and wraps;
				//on original do not use spellchecking only calculating wraps and cps;
				bool originalInTLMode = hasTLMode && txttl == L"";
				Misspells.Init(isRTL? convertedText : checkingText, SpellCheckerOn && !originalInTLMode,
					subsFormat, hideOverrideTags ? chtagLen : -1);
			}
			
			if (!isComment && subsFormat != TMP && !(CPS & visibleColumns)) {
				int chtime = Misspells.GetCPS(Dial);
				strings.push_back(wxString::Format(L"%i", chtime));
				shorttime = chtime > 15;
			}
			else if (subsFormat != TMP) {
				strings.push_back(L"");
				shorttime = false;
			}
			if (!isComment && !(WRAPS & visibleColumns)) {
				strings.push_back(Misspells.GetStrippedWraps());
				badWraps = Misspells.badWraps;
			}
			else {
				strings.push_back(L"");
				badWraps = false;
			}
			
			
			if (hideOverrideTags) {
				wxRegEx reg(subsFormat == SRT ? L"\\<[^\\<]*\\>" : L"\\{[^\\{]*\\}", wxRE_ADVANCED);
				if (!showOriginal && !isTl)
					reg.ReplaceAll(&txt, chtag);
				if ((!showOriginal && isTl) || showOriginal)
					reg.ReplaceAll(&txttl, chtag);
			}

			if (txt.length() > maxTextLength){ txt = txt.SubString(0, maxTextLength) + L"..."; }
			if (txttl.length() > maxTextLength){ txttl = txttl.SubString(0, maxTextLength) + L"..."; }
			strings.push_back((!showOriginal && isTl) ? txttl : txt);
			if (showOriginal){ strings.push_back(txttl); }

			isSelected = file->IsSelected(key);
			comparison = (Comparison && Comparison->at(key).size() > 0);
			bool comparisonMatch = (Comparison && !Comparison->at(key).differences);
			bool visibleLine = (Dial->Start.mstime <= VideoPos && Dial->End.mstime > VideoPos);
			col = (comparison) ? ComparisonBG :
				(comparisonMatch) ? ComparisonBGMatch :
				(visibleLine) ? visibleOnVideo :
				subsBkCol;
			if (isComment){ col = (comparison) ? ComparisonBGCmnt : (comparisonMatch) ? ComparisonBGCmntMatch : comment; }
			if (isSelected){
				col = GetColorWithAlpha(seldial, col);
			}
			visibleLines.push_back(visibleLine);
		}

		if (isFiltered){
			posX = 11;
			unsigned char hasHiddenBlock = file->CheckIfHasHiddenBlock(key, isHeadline);
			if (hasHiddenBlock){
				gc->SetBrush(*wxTRANSPARENT_BRUSH);
				gc->SetPen(textcol);
				int halfGridHeight = (GridHeight / 2);
				int newPosY = posY + GridHeight + 1;
				int startDrawPosY = newPosY + ((GridHeight - 10) / 2) - halfGridHeight;
				gc->DrawRectangle(1, startDrawPosY, 8, 8);
				gc->StrokeLine(3, newPosY - 1, 7, newPosY - 1);
				if (hasHiddenBlock == 1){
					gc->StrokeLine(5, startDrawPosY + 2, 5, startDrawPosY + 6);
				}
				//gc->SetPen(SpelcheckerCol);
				gc->StrokeLine(9, newPosY - 1, w + scHor, newPosY - 1);
			}
			if (Dial){
				if (!startBlock && Dial->isVisible == VISIBLE_BLOCK){
					startDrawPosYFromPlus = posY + 4; startBlock = true;
				}
				bool isLastLine = (id >= scrows - 2);
				bool notVisibleBlock = Dial->isVisible != VISIBLE_BLOCK;
				if (startBlock && (notVisibleBlock || isLastLine)){
					gc->SetBrush(*wxTRANSPARENT_BRUSH);
					gc->SetPen(textcol);
					int halfLine = posY - 1;
					if (isLastLine && !notVisibleBlock){ halfLine = posY + GridHeight; }
					gc->StrokeLine(5, startDrawPosYFromPlus, 5, halfLine);
					gc->StrokeLine(5, halfLine, w + scHor, halfLine);
					startBlock = false;
				}
			}


		}
		else{
			posX = 0;
		}

		numColumns = strings.size();
		

		wxRect cur;
		bool isCenter;
		wxColour label = (states == 0) ? labelBkColN : (states == 2) ? labelBkCol :
			(states == 1) ? labelBkColM : labelBkColD;
		for (int j = 0; j < numColumns; j++){
			if (key < scrollPosition){
				if (showOriginal && j == numColumns - 2){
					int half = (w + scHor - posX) / 2;
					GridWidth[j] = half;
					GridWidth[j + 1] = half;
					maxTextLength = (float)half / fontApproxSize;
				}

				if (!showOriginal && j == numColumns - 1){
					GridWidth[j] = w + scHor - posX;
					maxTextLength = (float)GridWidth[j] / fontApproxSize;
				}
			}


			if (GridWidth[j] < 1){
				continue;
			}
			gc->SetPen(*wxTRANSPARENT_PEN);

			gc->SetBrush(wxBrush((j == 0 && !isHeadline) ? label : col));
			if (unknownStyle && j == 4 || 
				shorttime && (j == 10 || (j == 3 && subsFormat != ASS && subsFormat != TMP)) ||
				badWraps && (j == 11 || (j == 4 && subsFormat > ASS && subsFormat != TMP) || (j == 2 && subsFormat == TMP))){
				gc->SetBrush(wxBrush(SpelcheckerCol));
			}

			gc->DrawRectangle(posX, posY, GridWidth[j], GridHeight);

			if (!isHeadline && j == numColumns - 1){
				wxString& text = (isRTL)? convertedText : strings[j];
				SpellErrors[key].DrawMisspells(text, wxPoint(posX, posY), gc, SpelcheckerCol, GridHeight);
				
				
				if (comparison){
					gc->SetFont(font, ComparisonCol);
					int cellSize = GridWidth[j];
					for (size_t c = 1; c < Comparison->at(key).size(); c += 2){
						wxString cmp = text.SubString(Comparison->at(key)[c], Comparison->at(key)[c + 1]);

						if (cmp == L""){ continue; }
						if (cmp == L" "){ cmp = L"_"; }
						wxString bcmp;
						if (Comparison->at(key)[c]>0){
							bcmp = text.Mid(0, Comparison->at(key)[c]);
							gc->GetTextExtent(bcmp, &bfw, &bfh);
						}
						else{ bfw = 0; }
						if (bfw + 3 > cellSize){
							break;
						}
						gc->GetTextExtent(cmp, &fw, &fh);

						gc->DrawTextU(cmp, posX + bfw + 2, posY);
						gc->DrawTextU(cmp, posX + bfw + 4, posY);
						gc->DrawTextU(cmp, posX + bfw + 2, posY + 2);
						gc->DrawTextU(cmp, posX + bfw + 4, posY + 2);
					}

				}

			}


			bool collis = (!isHeadline && acdial && key != currentLine &&
				(Dial->Start < acdial->End && Dial->End > acdial->Start));

			if (subsFormat < SRT){ isCenter = !(j == 4 || j == 5 || j == 9 || j > 10); }
			else if (subsFormat == TMP){ isCenter = (j == 0 || j == 1); }
			else{ isCenter = !(j == 4 || j == 5); }

			gc->SetFont(font, (isHeadline) ? headerText : (collis) ? collcol : textcol);

			int treeState = (Dial) ? Dial->treeState : 0;
			if (j > 0 && (treeState == TREE_DESCRIPTION || treeState == TREE_OPENED)){
				if (treeState == TREE_DESCRIPTION) {
					gc->SetBrush(comment);
					gc->SetPen(*wxTRANSPARENT_PEN);
					gc->DrawRectangle(posX + 1, posY, w - 1, GridHeight);
					// GetDialogueKey was made for loops no checks
					Dialogue *nextDial = (key < file->GetCount() - 1) ? file->GetDialogue(key + 1) : NULL;
					if (nextDial && nextDial->treeState == TREE_CLOSED) {
						wxBitmap bmpal(wxBITMAP_PNG(L"arrow_list"));
						gc->DrawBitmap(bmpal, posX + 6, posY + 5, bmpal.GetWidth(), bmpal.GetHeight());
					}
					else {
						wxBitmap bmp(wxBITMAP_PNG(L"arrow_list"));
						wxImage img = bmp.ConvertToImage();
						img = img.Rotate180();
						gc->DrawBitmap(img, posX + 6, posY + 5, img.GetWidth(), img.GetHeight());
						gc->SetBrush(*wxTRANSPARENT_BRUSH);
						gc->SetPen(textcol);
						gc->StrokeLine(posX, posY + GridHeight, w - 1, posY + GridHeight);
					}
					gc->DrawTextU(Dial->Text, posX + 23, posY + 1);
					break;
				}
				else {
					gc->SetBrush(*wxTRANSPARENT_BRUSH);
					gc->SetPen(textcol);
					Dialogue *nextDial = (key < file->GetCount() - 1) ? file->GetDialogue(key + 1) : NULL;

					gc->StrokeLine(posX, posY, posX, posY + GridHeight + 1);
					gc->StrokeLine(w - 1, posY, w - 1, posY + GridHeight + 1);
					if (!nextDial || nextDial->treeState < TREE_DESCRIPTION)
						gc->StrokeLine(posX, posY + GridHeight, w - 1, posY + GridHeight);
					
				}
				
			}
			//cur = wxRect(posX + 3, posY, GridWidth[j] - 6, GridHeight);
			//gc->Clip(cur);
			float centerPos = 0.f;
			if (isCenter){
				double fw, fh;
				gc->GetTextExtent(strings[j], &fw, &fh);
				centerPos = ((GridWidth[j] - fw) / 2) - 3;
			}
			gc->DrawTextU(strings[j], posX + 3 + centerPos, posY + 1);
			//gc->ResetClip();
			posX += GridWidth[j] + 1;


		}

		posY += GridHeight + 1;
		if (preview){
			if (posY >= previewpos.y - 2 && posY < previewpos.y + previewsize.y){
				posY = previewpos.y + previewsize.y + 3;
			}
			else if (posY > h){ scrows = id + 1; break; }
		}
		id++;
		key++;
	}

	posX = (isFiltered) ? 11 : 0;
	if (bg){
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->SetBrush(wxBrush(Options.GetColour(GRID_BACKGROUND)));
		gc->DrawRectangle(posX, posY, w + scHor, h);
	}
	if (size > 0){
		if (idmarkerPos != -1){
			gc->SetBrush(*wxTRANSPARENT_BRUSH);
			gc->SetPen(wxPen(Options.GetColour(GRID_ACTIVE_LINE)), 3.);
			int ypos = ((idmarkerPos - scrollPositionId + 1) * (GridHeight + 1));
			if (preview && ypos >= previewpos.y - 2){ ypos += previewsize.y + 5; }
			gc->DrawRectangle(posX + 1, ypos - 1, (GridWidth[0] - 1), GridHeight + 1);
		}

		if (idcurrentLine != -1){
			gc->SetBrush(*wxTRANSPARENT_BRUSH);
			gc->SetPen(wxPen(Options.GetColour(GRID_ACTIVE_LINE)));
			int ypos = ((idcurrentLine - scrollPositionId + 1) * (GridHeight + 1));
			if (preview && ypos >= previewpos.y - 2){ ypos += previewsize.y + 5; }
			gc->DrawRectangle(posX, ypos - 1, w + scHor - posX, GridHeight + 1);
		}
	}
	if (HasFocus()) {
		gc->SetBrush(*wxTRANSPARENT_BRUSH);
		gc->SetPen(wxPen(textcol));
		gc->DrawRectangle(0, 0, w + scHor - 1, h - 1);
	}
	delete gc;
}

void SubsGridWindow::RefreshColumns(int cell)
{
	AdjustWidths(cell);
	Refresh(false);
}

void SubsGridWindow::AdjustWidthsD2D(GraphicsContext *gc, int cell)
{

	int law = 0, startMax = 0, endMax = 0, stw = 0, edw = 0, syw = 0, acw = 0, efw = 0;
	double fw = 0, fh = 0;
	bool shml = false, shmr = false, shmv = false;

	int maxx = file->GetCount();

	gc->GetTextExtent(wxString::Format(L"%i", maxx), &fw, &fh);
	GridWidth[0] = fw + 10;
	if (!cell){
		delete gc;
		return;
	}

	Dialogue *dial;
	for (int i = 0; i < maxx; i++){
		dial = file->GetDialogue(i);
		if (!dial->isVisible){ continue; }
		if (first){
			if (dial->Format != subsFormat){ dial->Convert(subsFormat); }
			if (dial->Start.mstime > dial->End.mstime){
				dial->End.mstime = dial->Start.mstime;
			}
		}
		if (START & cell){
			if (dial->Start.mstime > startMax){ startMax = dial->Start.mstime; }
		}
		if ((END & cell) && subsFormat != TMP){
			if (dial->End.mstime > endMax){ endMax = dial->End.mstime; }
		}


		if (subsFormat<SRT){
			if ((LAYER & cell) && dial->Layer != 0){
				gc->GetTextExtent(wxString::Format(L"%i", dial->Layer), &fw, &fh);
				if (fw + 10>law){ law = fw + 10; }
			}
			if (STYLE & cell){
				gc->GetTextExtent(dial->Style, &fw, &fh);
				if (fw + 10 > syw){ syw = fw + 10; }
			}
			if ((ACTOR & cell) && dial->Actor != L""){
				gc->GetTextExtent(dial->Actor, &fw, &fh);
				if (fw + 10 > acw){ acw = fw + 10; }
			}
			if ((EFFECT & cell) && dial->Effect != L""){
				gc->GetTextExtent(dial->Effect, &fw, &fh);
				if (fw + 10 > efw){ efw = fw + 10; }
			}
			if ((MARGINL & cell) && dial->MarginL != 0){ shml = true; }
			if ((MARGINR & cell) && dial->MarginR != 0){ shmr = true; }
			if ((MARGINV & cell) && dial->MarginV != 0){ shmv = true; }
		}
	}


	if (START & cell){
		STime start(startMax);
		bool canShowFrames = showFrames;
		if (showFrames){
			Provider *FFMS2 = tab->Video->GetFFMS2();
			if (FFMS2)
				start.orgframe = FFMS2->GetFramefromMS(start.mstime);
			else
				canShowFrames = false;
		}
		gc->GetTextExtent(start.raw(canShowFrames ? FRAME : subsFormat), &fw, &fh);
		stw = fw + 10;
	}
	if (END & cell){
		STime end(endMax);
		bool canShowFrames = showFrames;
		if (showFrames){
			Provider *FFMS2 = tab->Video->GetFFMS2();
			if (FFMS2)
				end.orgframe = FFMS2->GetFramefromMS(end.mstime);
			else
				canShowFrames = false;
		}
		gc->GetTextExtent(end.raw(canShowFrames ? FRAME : subsFormat), &fw, &fh);
		edw = fw + 10;
	}

	if ((subsFormat < SRT) ? (LAYER & cell) : (START & cell)){
		wxString frst = (subsFormat < SRT) ? _("W.") : _("Start");
		gc->GetTextExtent(frst, &fw, &fh);
		GridWidth[1] = (subsFormat < SRT) ? law : stw;
		if (fw + 10 > GridWidth[1] && GridWidth[1] != 0){ GridWidth[1] = fw + 10; }
	}

	if ((subsFormat < SRT) ? (START & cell) : (END & cell)){
		wxString scnd = (subsFormat < SRT) ? _("Start") : _("Koniec");
		gc->GetTextExtent(scnd, &fw, &fh);
		GridWidth[2] = (subsFormat < SRT) ? stw : edw;
		if (fw + 10 > GridWidth[2]){ GridWidth[2] = fw + 10; };
	}
	if (subsFormat<SRT){
		if (END & cell){
			gc->GetTextExtent(_("Koniec"), &fw, &fh);
			GridWidth[3] = edw;
			if (fw + 10>GridWidth[3]){ GridWidth[3] = fw + 10; };
		}

		if (STYLE & cell){
			gc->GetTextExtent(_("Styl"), &fw, &fh);
			GridWidth[4] = syw;
			if (fw + 10 > GridWidth[4]){ GridWidth[4] = fw + 10; }
		}

		if (ACTOR & cell){
			gc->GetTextExtent(_("Aktor"), &fw, &fh);
			if (fw + 10 > acw&&acw != 0){ acw = fw + 10; };
			GridWidth[5] = (acw == 0) ? 0 : acw;
		}

		if (224 & cell){
			gc->GetTextExtent(_("M.Pi."), &fw, &fh);
			if (MARGINL & cell){ GridWidth[6] = (!shml) ? 0 : fw + 10; }
			if (MARGINR & cell){ GridWidth[7] = (!shmr) ? 0 : fw + 10; }
			if (MARGINV & cell){ GridWidth[8] = (!shmv) ? 0 : fw + 10; }
		}

		if (EFFECT & cell){
			gc->GetTextExtent(_("Efekt"), &fw, &fh);
			if (fw + 10 > efw&&efw != 0){ efw = fw + 10; };
			GridWidth[9] = (efw == 0) ? 0 : efw;
		}
	}

	if (CPS & cell){
		gc->GetTextExtent(_("ZNS"), &fw, &fh);
		GridWidth[(subsFormat < SRT) ? 10 : 3] = fw + 5;
	}

	if (WRAPS & cell) {
		gc->GetTextExtent(_("Łamania"), &fw, &fh);
		GridWidth[(subsFormat < SRT) ? 11 : (subsFormat != TMP) ? 4 : 2] = fw + 5;
	}

	if (subsFormat == TMP) {
		GridWidth[3] = 0; GridWidth[10] = 0;  GridWidth[11] = 0;
	}
	if (subsFormat > ASS){GridWidth[5] = 0; GridWidth[6] = 0; GridWidth[7] = 0; GridWidth[8] = 0; GridWidth[9] = 0; }
	if ((subsFormat < SRT) ? (LAYER & visibleColumns) : (START & visibleColumns)){ GridWidth[1] = 0; }
	if ((subsFormat < SRT) ? (START & visibleColumns) : (subsFormat != TMP) ? (END & visibleColumns) : (WRAPS & visibleColumns)){ GridWidth[2] = 0; }
	if ((subsFormat < SRT) ? (END & visibleColumns) : (CPS & visibleColumns)){ GridWidth[3] = 0; }
	if (STYLE & visibleColumns){ GridWidth[4] = 0; }
	if (ACTOR & visibleColumns){ GridWidth[5] = 0; }
	if (MARGINL & visibleColumns){ GridWidth[6] = 0; }
	if (MARGINR & visibleColumns){ GridWidth[7] = 0; }
	if (MARGINV & visibleColumns){ GridWidth[8] = 0; }
	if (EFFECT & visibleColumns){ GridWidth[9] = 0; }
	if (CPS & visibleColumns){ GridWidth[10] = 0; }
	if (WRAPS & visibleColumns) { GridWidth[11] = 0; }
	first = false;
	delete gc;
}

void SubsGridWindow::AdjustWidths(int cell)
{
	GraphicsRenderer *renderer = GraphicsRenderer::GetDirect2DRenderer();
	GraphicsContext* gc = renderer? renderer->CreateMeasuringContext() : NULL;
	if (gc){
		gc->SetFont(font, L"#FFFFFF");
		AdjustWidthsD2D(gc, cell);
		return;
	}
	wxClientDC dc(this);
	dc.SetFont(font);

	int law = 0, startMax = 0, endMax = 0, stw = 0, edw = 0, syw = 0, acw = 0, efw = 0, fw = 0, fh = 0;
	bool shml = false, shmr = false, shmv = false;

	int maxx = file->GetCount();

	dc.GetTextExtent(wxString::Format(L"%i", maxx), &fw, &fh);
	GridWidth[0] = fw + 10;
	if (!cell)
		return;

	Dialogue *dial;
	for (int i = 0; i < maxx; i++){
		dial = file->GetDialogue(i);
		if (!dial->isVisible){ continue; }
		if (first){
			if (dial->Format != subsFormat){ dial->Convert(subsFormat); }
			if (dial->Start.mstime > dial->End.mstime){
				dial->End.mstime = dial->Start.mstime;
			}
		}
		if (START & cell){
			if (dial->Start.mstime > startMax){ startMax = dial->Start.mstime; }
		}
		if ((END & cell) && subsFormat != TMP){
			if (dial->End.mstime > endMax){ endMax = dial->End.mstime; }
		}


		if (subsFormat<SRT){
			if ((LAYER & cell) && dial->Layer != 0){
				dc.GetTextExtent(wxString::Format(L"%i", dial->Layer), &fw, &fh);
				if (fw + 10>law){ law = fw + 10; }
			}
			if (STYLE & cell){
				dc.GetTextExtent(dial->Style, &fw, &fh);
				if (fw + 10 > syw){ syw = fw + 10; }
			}
			if ((ACTOR & cell) && dial->Actor != L""){
				dc.GetTextExtent(dial->Actor, &fw, &fh);
				if (fw + 10 > acw){ acw = fw + 10; }
			}
			if ((EFFECT & cell) && dial->Effect != L""){
				dc.GetTextExtent(dial->Effect, &fw, &fh);
				if (fw + 10 > efw){ efw = fw + 10; }
			}
			if ((MARGINL & cell) && dial->MarginL != 0){ shml = true; }
			if ((MARGINR & cell) && dial->MarginR != 0){ shmr = true; }
			if ((MARGINV & cell) && dial->MarginV != 0){ shmv = true; }
		}
	}


	if (START & cell){
		STime start(startMax);
		bool canShowFrames = showFrames;
		if (showFrames){
			Provider *FFMS2 = tab->Video->GetFFMS2();
			if (FFMS2)
				start.orgframe = FFMS2->GetFramefromMS(start.mstime);
			else
				canShowFrames = false;
		}
		dc.GetTextExtent(start.raw(canShowFrames ? FRAME : subsFormat), &fw, &fh);
		stw = fw + 10;
	}
	if (END & cell){
		STime end(endMax);
		bool canShowFrames = showFrames;
		if (showFrames){
			Provider *FFMS2 = tab->Video->GetFFMS2();
			if (FFMS2)
				end.orgframe = FFMS2->GetFramefromMS(end.mstime);
			else
				canShowFrames = false;
		}
		dc.GetTextExtent(end.raw(canShowFrames ? FRAME : subsFormat), &fw, &fh);
		edw = fw + 10;
	}

	if ((subsFormat < SRT) ? (LAYER & cell) : (START & cell)){
		wxString frst = (subsFormat < SRT) ? _("W.") : _("Start");
		dc.GetTextExtent(frst, &fw, &fh);
		GridWidth[1] = (subsFormat < SRT) ? law : stw;
		if (fw + 10 > GridWidth[1] && GridWidth[1] != 0){ GridWidth[1] = fw + 10; }
	}

	if ((subsFormat < SRT) ? (START & cell) : (END & cell)){
		wxString scnd = (subsFormat < SRT) ? _("Start") : _("Koniec");
		dc.GetTextExtent(scnd, &fw, &fh);
		GridWidth[2] = (subsFormat < SRT) ? stw : edw;
		if (fw + 10 > GridWidth[2]){ GridWidth[2] = fw + 10; };
	}
	if (subsFormat < SRT){
		if (END & cell){
			dc.GetTextExtent(_("Koniec"), &fw, &fh);
			GridWidth[3] = edw;
			if (fw + 10>GridWidth[3]){ GridWidth[3] = fw + 10; };
		}

		if (STYLE & cell){
			dc.GetTextExtent(_("Styl"), &fw, &fh);
			GridWidth[4] = syw;
			if (fw + 10 > GridWidth[4]){ GridWidth[4] = fw + 10; }
		}

		if (ACTOR & cell){
			dc.GetTextExtent(_("Aktor"), &fw, &fh);
			if (fw + 10 > acw&&acw != 0){ acw = fw + 10; };
			GridWidth[5] = (acw == 0) ? 0 : acw;
		}

		if (224 & cell){
			dc.GetTextExtent(_("M.Pi."), &fw, &fh);
			if (MARGINL & cell){ GridWidth[6] = (!shml) ? 0 : fw + 10; }
			if (MARGINR & cell){ GridWidth[7] = (!shmr) ? 0 : fw + 10; }
			if (MARGINV & cell){ GridWidth[8] = (!shmv) ? 0 : fw + 10; }
		}

		if (EFFECT & cell){
			dc.GetTextExtent(_("Efekt"), &fw, &fh);
			if (fw + 10 > efw&&efw != 0){ efw = fw + 10; };
			GridWidth[9] = (efw == 0) ? 0 : efw;
		}
	}

	if (CPS & cell){
		dc.GetTextExtent(_("ZNS"), &fw, &fh);
		GridWidth[(subsFormat < SRT) ? 10 : 3] = fw + 5;
	}

	if (WRAPS & cell) {
		dc.GetTextExtent(_("Łamania"), &fw, &fh);
		GridWidth[(subsFormat < SRT) ? 11 : (subsFormat != TMP) ? 4 : 2] = fw + 5;
	}

	if (subsFormat == TMP) {
		GridWidth[3] = 0; GridWidth[10] = 0;  GridWidth[11] = 0;
	}
	if (subsFormat > ASS) { GridWidth[5] = 0; GridWidth[6] = 0; GridWidth[7] = 0; GridWidth[8] = 0; GridWidth[9] = 0; }
	if ((subsFormat < SRT) ? (LAYER & visibleColumns) : (START & visibleColumns)) { GridWidth[1] = 0; }
	if ((subsFormat < SRT) ? (START & visibleColumns) : (subsFormat != TMP) ? (END & visibleColumns) : (WRAPS & visibleColumns)) { GridWidth[2] = 0; }
	if ((subsFormat < SRT) ? (END & visibleColumns) : (CPS & visibleColumns)) { GridWidth[3] = 0; }
	if (STYLE & visibleColumns) { GridWidth[4] = 0; }
	if (ACTOR & visibleColumns) { GridWidth[5] = 0; }
	if (MARGINL & visibleColumns) { GridWidth[6] = 0; }
	if (MARGINR & visibleColumns) { GridWidth[7] = 0; }
	if (MARGINV & visibleColumns) { GridWidth[8] = 0; }
	if (EFFECT & visibleColumns) { GridWidth[9] = 0; }
	if (CPS & visibleColumns) { GridWidth[10] = 0; }
	if (WRAPS & visibleColumns) { GridWidth[11] = 0; }
	first = false;

}


void SubsGridWindow::SetVideoLineTime(wxMouseEvent &evt, int mvtal)
{
	if (tab->Video->GetState() != None){
		if (tab->Video->GetState() != Paused){
			if (tab->Video->GetState() == Stopped){ tab->Video->Play(); tab->Video->Pause(); }
			else if (mvtal)
				tab->Video->Pause();
		}
		short wh = (subsFormat < SRT) ? 2 : 1;
		int whh = 2;
		for (int i = 0; i <= wh; i++){ whh += GridWidth[i]; }
		whh -= scHor;
		if (isFiltered){ whh += 11; }
		bool isstart;
		int vczas;
		bool getEndTime = evt.GetX() >= whh && evt.GetX() < whh + GridWidth[wh + 1] && subsFormat != TMP;
		if (getEndTime){
			vczas = Edit->line->End.mstime; isstart = false;
		}
		else{
			vczas = Edit->line->Start.mstime; isstart = true;
		}
		if (evt.LeftDClick() && evt.ControlDown()){ vczas -= 1000; }
		tab->Video->Seek(MAX(0, vczas), isstart, true, false);
		if (Edit->ABox){ Edit->ABox->audioDisplay->Update(getEndTime); }
		if (Edit->Visual > CHANGEPOS){
			tab->Video->SetVisual(true, true);
		}
	}
}

void SubsGridWindow::SetActive(int active)
{
	SelectRow(active);
	lastRow = active;
	markedLine = active;
	Edit->SetLine(active);
}

void SubsGridWindow::OnMouseEvent(wxMouseEvent &event) {

	int w, h;
	GetClientSize(&w, &h);



	bool shift = event.ShiftDown();
	bool alt = event.AltDown();
	bool ctrl = event.CmdDown();

	bool click = event.LeftDown();
	bool left_up = event.LeftUp();
	bool dclick = event.LeftDClick();
	bool middle = event.MiddleDown();
	bool right = event.RightDown();
	int curY = event.GetY();
	if (preview){
		if (event.ButtonDown())
			Edit->SetGrid((SubsGrid*)this);

		wxPoint previewpos = preview->GetPosition();
		wxSize previewsize = preview->GetSize();
		if (curY >= previewpos.y - 2 && curY <= previewpos.y + previewsize.y + 1){ return; }
		if (curY > previewpos.y + previewsize.y){
			curY -= previewsize.y + 3;
		}
	}
	int curX = (event.GetX());
	int size = GetCount();

	if (ismenushown){ ScreenToClient(&curX, &curY); }
	int row = GetKeyFromScrollPos(curY / (GridHeight + 1)) - 1;
	//int rowId = curY / (GridHeight + 1) + scrollPosition - 1;
	bool outOfPosition = (row < scrollPosition || row >= size);
	int hideColumnWidth = (isFiltered) ? 12 : 0;
	bool isNumerizeColumn = (curX >= hideColumnWidth && curX < GridWidth[0] + hideColumnWidth);

	if (left_up && !holding) {
		return;
	}

	// Get focus
	if (event.ButtonDown()) {
		SetFocus();
	}

	// Mouse wheel
	if (event.GetWheelRotation() != 0) {
		int step = 3 * event.GetWheelRotation() / event.GetWheelDelta();
		ScrollTo(scrollPosition, false, -step, true);
		if (Comparison)
			ShowSecondComparedLine(scrollPosition, false, false, true);
		return;
	}

	//Check if it is tree description line
	if (file->CheckIfIsTree(row)){
		if (event.GetModifiers() == 0){
			if (click){
				int diff = file->OpenCloseTree(row);
				RefreshColumns();
				if (SpellErrors.size() > (row + 1))
					SpellErrors.erase(SpellErrors.begin() + (row + 1), SpellErrors.end());

				if (currentLine > row){
					size_t firstSel = FirstSelection();
					if (firstSel == -1){
						if (currentLine < size)
							file->InsertSelection(currentLine);
						else
							Edit->SetLine(size - 1);
					}
					else
						Edit->SetLine(firstSel);
				}
			}
			else if (right){
				ContextMenuTree(event.GetPosition(), row);
			}
		}
		return;
	}

	// Seeking video by click on numeration column
	if ((click || dclick) && isNumerizeColumn){
		if (tab->Video->GetState() != None && !outOfPosition){
			if (tab->Video->GetState() != Paused){
				if (tab->Video->GetState() == Stopped){ tab->Video->Play(); }
				tab->Video->Pause();
			}
			int vtime = 0;
			bool isstart = true;
			if (shift && subsFormat != TMP){
				vtime = GetDialogue(row)->End.mstime; isstart = false;
			}
			else{
				vtime = GetDialogue(row)->Start.mstime; isstart = true;
			}
			if (ctrl){ vtime -= 1000; }
			tab->Video->Seek(MAX(0, vtime), isstart, true, false, true, false);
			if (Edit->ABox){ Edit->ABox->audioDisplay->Update(shift && subsFormat != TMP); }
			if (Edit->Visual > CHANGEPOS){
				tab->Video->SetVisual(true, true);
			}
		}
		return;
	}
	// Changing marked line & popup
	if (right && !ctrl) {
		if (isNumerizeColumn){
			markedLine = row;
			Refresh(false);

		}
		return;
	}
	else if (event.RightUp() && !isNumerizeColumn){
		ContextMenu(event.GetPosition());
		return;
	}


	if (left_up && holding) {
		holding = false;
		//Save swap lines after alt release 
		if (event.AltDown() && file->IsNotSaved()){ 
			SetModified(GRID_SWAP); 
		}
		ReleaseMouse();
		if (oldX != -1){ return; }
	}



	if (curX < hideColumnWidth){
		int filterRow = GetKeyFromScrollPos(((curY + (GridHeight / 2)) / (GridHeight + 1)) - 1) - 1;
		if (filterRow < size && curY > (GridHeight / 2)) {
			//hack for first line it's lame solution
			if ((click || dclick)){
				unsigned char state = file->CheckIfHasHiddenBlock(filterRow, filterRow < scrollPosition);
				if (state){
					SubsGridFiltering filter((SubsGrid*)this, currentLine);
					//second part of hack
					if (filterRow < scrollPosition){
						if (state == 1){
							filterRow = GetKeyFromPosition(filterRow, -1, false);
							scrollPosition = filterRow? filterRow + 1 : filterRow;
							scrollPositionId = file->GetElementByKey(scrollPosition);
						}
						else{
							scrollPositionId += 1;
							scrollPosition = GetKeyFromPosition(scrollPosition, 1);
						}
					}

					filter.FilterPartial(filterRow);
				}
			}
		}
		return;
	}
	// Click type
	else if (click && curX >= hideColumnWidth) {
		holding = true;
		if (!shift) lastRow = row;
		lastsel = row;
		oldX = (curY < GridHeight) ? curX : -1;
		CaptureMouse();
	}
	if (holding && oldX != -1){
		int diff = (oldX - curX);
		if ((scHor == 0 && diff < 0) || diff == 0 || (scHor > 1500 && diff > 0)){ return; }
		scHor = scHor + diff;
		oldX = curX;
		if (scHor < 0){ scHor = 0; }
		Refresh(false);
		return;
	}
	VideoCtrl *video = tab->Video;
	bool changeActive = Options.GetBool(GRID_CHANGE_ACTIVE_ON_SELECTION);
	int seekAfter = 0, playAfter = 0;
	video->GetVideoListsOptions(&playAfter, &seekAfter);
	if (!outOfPosition) {
		if (holding && alt)
		{
			if (lastsel != -1 && lastsel != row) {	
				if (!file->edited)
					SaveSelections();
				file->edited |= MoveRows(file->GetElementByKey(row) - file->GetElementByKey(lastsel));
			}
			lastsel = row;
			if (click){
				return;
			}
		}


		// Toggle selected
		if (left_up && ctrl && !shift && !alt) {
			if (!(currentLine == row && file->SelectionsSize() == 1 && file->IsSelected(row))){
				SelectRow(row, true, !file->IsSelected(row));
				if (file->SelectionsSize() < 1){ SelectRow(currentLine); }
				return;
			}

		}


		// Normal click
		if (!shift && !alt) {

			if (click && (changeActive || !ctrl) || (dclick && ctrl)) {
				lastActiveLine = currentLine;
				Edit->SetLine(row, true, true, true, !ctrl);
				if (hasTLMode){ Edit->SetActiveLineToDoubtful(); }
				if (changeActive){ Refresh(false); }
				if (!ctrl || dclick){
					SelectRow(row);
					extendRow = -1;
				}
				if (!ctrl){
					if (Comparison){ ShowSecondComparedLine(row); }
					else if (preview){ preview->NewSeeking(); }
				}
			}

			//1-left click
			//2-left click and edition on pause
			//3-left click and edition on pause and play

			if (dclick || (click && lastActiveLine != row && (changeActive || !ctrl) && seekAfter < 4 && seekAfter > 0) && playAfter < 2){
				SetVideoLineTime(event, seekAfter);
			}

			if (click || dclick || left_up)
				return;
		}

		if (middle){
			if (ctrl){
				lastActiveLine = currentLine;
				Edit->SetLine(row, true, true, true, !ctrl);
				if (hasTLMode){ Edit->SetActiveLineToDoubtful(); }
				Refresh(false);
				extendRow = -1;
				if (Comparison){ ShowSecondComparedLine(row); }
				else if (preview){ preview->NewSeeking(); }
			}
			else if (video->GetState() != None){
				video->PlayLine(GetDialogue(row)->Start.mstime, video->GetPlayEndTime(GetDialogue(row)->End.mstime));
			}

		}
	}

	// Scroll to keep visibleColumns
	if (holding) {
		
		// End the hold if this was a mousedown to avoid accidental
		// selection of extra lines
		if (click) {
			holding = false;
			left_up = true;
			ReleaseMouse();
		}else
			MakeVisible(row);
	}

	// Block select
	if ((left_up && shift && !alt) || (holding && !ctrl && !alt && !shift && lastsel != row)) {
		if (lastRow != -1) {
			// Keyboard selection continues from where the mouse was last used
			extendRow = lastRow;

			// Set boundaries
			row = MID(0, row, size - 1);
			int i1 = row;
			int i2 = lastRow;
			if (i1 > i2) {
				int aux = i1;
				i1 = i2;
				i2 = aux;
			}

			// Toggle each
			file->InsertSelections(i1, i2, !ctrl);
			if (changeActive){
				lastActiveLine = currentLine;
				Edit->SetLine(row, true, true, false);
				if (hasTLMode){ Edit->SetActiveLineToDoubtful(); }
			}
			lastsel = row;
			Refresh(false);
			if (Edit->Visual == CHANGEPOS){
				video->SetVisual(false, true);
			}
		}
	}


}

void SubsGridWindow::OnScroll(wxScrollWinEvent& event)
{
	int newPos = event.GetPosition();
	if (scrollPositionId != newPos) {
		scrollPositionId = newPos;
		scrollPosition = file->GetElementById(newPos);
		Refresh(false);
		Update();
		if (Comparison)
			ShowSecondComparedLine(scrollPosition, false, false, true);
	}
}

void SubsGridWindow::OnSize(wxSizeEvent& event)
{
	if (preview){
		wxSize size = GetClientSize();
		preview->SetSize(wxSize(size.x, -1));
	}
	Refresh(false);
	Update();
}

void SubsGridWindow::SelectRow(int row, bool addToSelected /*= false*/, bool select /*= true*/, bool norefresh /*= false*/)
{
	row = MID(0, row, GetCount() - 1);
	if (addToSelected){
		if (!select){ file->EraseSelection(row); }
		else{ file->InsertSelection(row); }
		if (norefresh){ return; }
		int w = 0;
		int h = 0;
		GetClientSize(&w, &h);
		/*if (row >= scrollPosition){
			size_t rowkey = GetKeyFromPosition(scrollPosition, row + 1 - scrollPosition);
			RefreshRect(wxRect(0, (rowkey + 1 - scrollPosition) * (GridHeight + 1), w, GridHeight + 1), false);
		}*/
		Refresh(false);
	}
	else{
		file->ClearSelections();
		file->InsertSelection(row);
		if (norefresh){ return; }
		Refresh(false);
	}
	
	if (Edit->Visual == CHANGEPOS){
		tab->Video->SetVisual(false, true);
	}
}

void SubsGridWindow::ScrollTo(int y, bool center /*= false*/, int offset /*= 0*/, bool useUpdate/* = false*/){
	int w, h;
	GetClientSize(&w, &h);
	if (offset){ y = GetKeyFromPosition(y, offset); }
	else if (center){ 
		y = GetKeyFromPosition(y, -(h / (GridHeight + 1)) / 2); 
		if (preview){
			wxSize previewsize = preview->GetSize();
			int previewMovement = (previewsize.y / (GridHeight + 1)) + 1;
			if (scrollPositionId > previewMovement)
				y = GetKeyFromPosition(y, (previewMovement / 2));
		}
	}
	//int size = GetCount() + 2;
	
	// need to calculate this
	int nextY = MID(0, y, GetCount() - 1);

	if (scrollPosition != nextY) {
		scrollPosition = nextY;
		scrollPositionId = file->GetElementByKey(nextY);
	}
	Refresh(false);
	if (useUpdate)
		Update();
}

void SubsGridWindow::OnKeyPress(wxKeyEvent &event) {
	// Get size
	int w, h;
	GetClientSize(&w, &h);

	// Get scan code
	int key = event.GetKeyCode();
	bool ctrl = event.m_controlDown;
	bool alt = event.m_altDown;
	bool shift = event.m_shiftDown;

	// The "menu" key, simulate a right-click
	if (key == WXK_WINDOWS_MENU) {
		wxPoint pos;
		pos.x = w / 2;
		pos.y = h / 2;
		ContextMenu(pos);
		return;
	}
	else if (key == WXK_TAB) {
		wxNavigationKeyEvent evt;
		evt.SetDirection(!event.ShiftDown());
		evt.SetWindowChange(event.ControlDown());
		evt.SetFromTab(true);
		evt.SetEventObject(this);
		wxWindow* win = GetParent();
		while (win) {
			if (win->GetEventHandler()->ProcessEvent(evt))
				break;
			win = win->GetParent();
		}
		return;
	}

	// Select all
	if (key == L'A' && ctrl && !alt && !shift) {
		file->InsertSelections(0, -1);
		Refresh(false);
	}

	// Up/down
	int dir = 0;
	if (key == WXK_UP) dir = -1;
	if (key == WXK_DOWN) dir = 1;
	if (key == WXK_PAGEUP) {
		dir = -(h / GridHeight - 1);
	}
	if (key == WXK_PAGEDOWN) {
		dir = h / GridHeight - 1;
	}
	if (key == WXK_HOME) {
		dir = -((signed)GetCount());
	}
	if (key == WXK_END) {
		dir = GetCount();
	}
	if (key == WXK_RETURN){
		Edit->TextEdit->SetFocus();
	}

	// Moving
	if (dir) {
		// Move selection
		if (!ctrl && !shift && !alt) {
			// Move to extent first
			int curLine = currentLine;
			if (extendRow != -1) {
				curLine = extendRow;
				extendRow = -1;
			}

			int next = GetKeyFromPosition(currentLine, dir);
			Edit->SetLine(next);
			SelectRow(next);
			MakeVisible(next);
			if (Comparison){ ShowSecondComparedLine(next); }
			else if (preview){ preview->NewSeeking(); }
			lastRow = next;
			//return;
		}


		// Move selected
		else if (alt && !shift) {
			if (FirstSelection() != -1){
				SaveSelections();
				if (MoveRows(dir)){
					file->edited = true;
					SetModified(GRID_SWAP);
				}
			}
			return;
		}

		// Shift-selection
		else if (shift && !ctrl && !alt) {
			// Find end
			if (extendRow == -1) extendRow = lastRow = currentLine;
			bool changeActive = Options.GetBool(GRID_CHANGE_ACTIVE_ON_SELECTION);

			int i1 = currentLine;

			if (!changeActive) {
				extendRow = lastRow = GetKeyFromPosition(extendRow, dir);
			}
			else {
				i1 = GetKeyFromPosition(currentLine, dir);
			}
			
			// Set range
			int i2 = extendRow;
			int i11 = i1;
			if (i2 < i1) {
				int aux = i1;
				i1 = i2;
				i2 = aux;
			}

			file->InsertSelections(i1, i2, true);
			if (changeActive) {
				lastActiveLine = currentLine;
				Edit->SetLine(i11, true, true, false);
			}
			MakeVisible(changeActive? currentLine : extendRow);
		}
		if (hasTLMode){ Edit->SetActiveLineToDoubtful(); }
	}

}



void SubsGridWindow::RefreshIfVisible(int time)
{
	size_t counter = 0;
	//make it work properly
	for (size_t i = scrollPosition; i < file->GetCount() && counter < visibleLines.size(); i++){
		Dialogue *dial = GetDialogue(i);
		if (!dial->isVisible)
			continue;

		bool isVisible = dial->Start.mstime <= time && dial->End.mstime > time;
		if (isVisible != visibleLines[counter]){
			Refresh(false);
			break;
		}
		counter++;
	}

}

void SubsGridWindow::ChangeTimeDisplay(bool frame)
{
	if (frame){
		showFrames = true;
	}
	else{
		showFrames = false;
	}

}


void SubsGridWindow::HideOverrideTags()
{
	hideOverrideTags = !hideOverrideTags;
	Options.SetBool(GRID_HIDE_TAGS, hideOverrideTags);
	SpellErrors.clear();
	Refresh(false);
}


void SubsGridWindow::ChangeActiveLine(int newActiveLine, bool refresh /*= false*/, bool scroll /*= false*/, bool changeEditboxLine /*= true*/)
{
	if (changeEditboxLine)
		Edit->SetLine(newActiveLine);
	//there is no editbox change cause of loaded in editbox preview
	else
		currentLine = markedLine = newActiveLine;
	SelectRow(newActiveLine, false, true, true);
	if (scroll)
		ScrollTo(newActiveLine, true);
	if (refresh)
		Refresh(false);
}

void SubsGridWindow::SelVideoLine(int curtime)
{
	if (tab->Video->GetState() == None && curtime < 0){ return; }

	int time = (curtime < 0) ? tab->Video->Tell() : curtime;
	int prevtime = 0;
	int durtime = (curtime < 0) ? tab->Video->GetDuration() : 36000000;
	int idr = 0, ip = 0;

	for (size_t i = 0; i < GetCount(); i++)
	{
		Dialogue *dial = GetDialogue(i);
		if (!dial->isVisible)
			continue;

		if (!dial->IsComment && (dial->Text != L"" || dial->TextTl != L"")){
			if (time >= dial->Start.mstime && time <= dial->End.mstime)
			{
				Edit->SetLine(i); 
				SelectRow(i); 
				MakeVisible(i);
				return;
			}
			if (dial->Start.mstime > prevtime && dial->Start.mstime < time){ prevtime = dial->Start.mstime; ip = i; }
			if (dial->Start.mstime < durtime && dial->Start.mstime > time){ durtime = dial->Start.mstime; idr = i; }

		}
		
	}
	
	if ((time - prevtime) > (durtime - time)){
		Edit->SetLine(idr);
		SelectRow(idr);
		MakeVisible(idr);
	}
	else{
		Edit->SetLine(ip);
		SelectRow(ip);
		MakeVisible(ip);
	}

}

void SubsGridWindow::ShowSecondComparedLine(int Line, bool showPreview, bool fromPreview, bool setViaScroll)
{
	SubsGrid *thisgrid = (SubsGrid*)this;
	SubsGrid *secondgrid = NULL;
	if (thisgrid == CG1)
		secondgrid = CG2;
	else if (thisgrid == CG2)
		secondgrid = CG1;
	else
		return;

	bool hiddenSecondGrid = !secondgrid->IsShownOnScreen();
	if (!(showPreview || preview) && hiddenSecondGrid){ return; }
	
	compareData & data = Comparison->at(Line);
	int secondGridLine = data.secondComparedLine;
	if (secondGridLine < 0){ return; }
	if (setViaScroll){
		secondgrid->scrollPosition = secondGridLine;
		secondgrid->scrollPositionId = file->GetElementByKey(secondGridLine);
		secondgrid->Refresh(false);
		secondgrid->Update();
		return;
	}
	int diffPosition = Line - scrollPosition;
	secondgrid->scrollPosition = secondGridLine - diffPosition;
	secondgrid->ChangeActiveLine(secondGridLine, true, fromPreview, !fromPreview);
	if (!fromPreview && hiddenSecondGrid){
		if (!preview){
			ShowPreviewWindow(secondgrid, thisgrid, Line, diffPosition);
		}
		else{
			preview->MakeVisible();
			preview->Refresh(false);
		}
	}
}

void SubsGridWindow::RefreshPreview()
{
	if (preview)
		preview->Refresh(false);
}

bool SubsGridWindow::ShowPreviewWindow(SubsGrid *previewGrid, SubsGrid *windowToDraw, int activeLine, int diffPosition)
{
	int w, h;
	GetClientSize(&w, &h);
	int realGridHeight = (GridHeight + 1);
	int previewHeight = (((h / 3) / realGridHeight) * realGridHeight) + realGridHeight + 4;
	if (previewHeight < 100)
		previewHeight = ((100 / realGridHeight) * realGridHeight) + realGridHeight + 4;
	if (h < 150){ KaiMessageBox(_("Nie można wyświetlić podglądu, ponieważ wielkość okna napisów jest zbyt mała")); return false; }
	int previewPosition = (diffPosition + 2) * realGridHeight;
	if (previewPosition + previewHeight > h || previewPosition < 20){
		int newLine = (((h - previewHeight) / 2) / realGridHeight);
		scrollPosition = (activeLine - newLine) + 2;
		previewPosition = newLine * realGridHeight;
	}
	preview = new SubsGridPreview(previewGrid, windowToDraw, previewPosition + 2, wxSize(w, previewHeight));
	Refresh(false);
	return true;
}

void SubsGridWindow::MakeVisible(int rowKey)
{
	int position = rowKey;//(rowKey != -1) ? rowKey : currentLine;
	int w, h;
	GetClientSize(&w, &h);
	// Find direction
	int scdelta = 3;
	int minVis = GetKeyFromPosition(scrollPosition, 1);
	int maxDelta = (h / (GridHeight + 1)) - 2;
	if (preview){
		wxSize previewsize = preview->GetSize();
		maxDelta -= (previewsize.y / (GridHeight + 1)) + 1;
	}
	int maxVis = GetKeyFromPosition(scrollPosition, maxDelta);
	int delta = 0;
	//scdelta = (position - scrollPosition) + 3;
	int newPosition = 0;
	if (position < minVis) {
		delta = -scdelta;
		newPosition = position;
	}

	if (position > maxVis){
		delta = scdelta - maxDelta;
		newPosition = position;
	}
	//make it works on first line when pos = 0 and delta is negative
	if (delta && !(newPosition == 0 && newPosition == scrollPosition)) {
		ScrollTo(newPosition, false, delta);
	}
	else {//make sure to refresh, in some cases it causes bugs
		Refresh(false);
	}
}