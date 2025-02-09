﻿//  Copyright (c) 2018-2020, Marcin Drob

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

#include "Config.h"
#include "FindReplaceDialog.h"
#include "FindReplace.h"
#include "KainoteMain.h"
#include "Stylelistbox.h"

TabWindow::TabWindow(wxWindow *parent, int id, int tabNum, FindReplace * _FR)
	: wxWindow(parent, id)
	, windowType(tabNum)
	, FR(_FR)
{
	//SetMinSize(wxSize(850, 400));
	int options = Options.GetInt(FIND_REPLACE_OPTIONS);

	wxBoxSizer* mainfrbsizer = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* mainfrbsizer1 = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* mainfrbsizer2 = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* mainfrbsizer3 = new wxBoxSizer(wxHORIZONTAL);

	//find list and description
	wxBoxSizer* frsbsizer = new wxBoxSizer(wxHORIZONTAL);
	FindText = new KaiChoice(this, ID_FIND_TEXT, L"", wxDefaultPosition, wxSize(276, -1), FR->findRecent);
	FindText->SetToolTip(_("Szukany tekst:"));
	FindText->SetMaxLength(MAXINT);
	FindText->SetSelection(0);
	frsbsizer->Add(new KaiStaticText(this, -1, _("Szukany tekst:"), wxDefaultPosition, wxSize(-1/*80*/, -1)),
		1, wxALIGN_CENTER_VERTICAL/* | wxALIGN_RIGHT*/ | wxRIGHT, 4, 0);
	frsbsizer->Add(FindText, 3, wxEXPAND, 0);
	mainfrbsizer1->Add(frsbsizer, 0, wxEXPAND | wxALL, 4);

	if (tabNum != WINDOW_FIND){
		//replace list and description blocked on window find
		wxBoxSizer *ReplaceStaticSizer = new wxBoxSizer(wxHORIZONTAL);
		ReplaceText = new KaiChoice(this, ID_REPLACE_TEXT, L"", wxDefaultPosition, wxSize(276, -1), FR->replaceRecent);
		ReplaceText->SetToolTip(_("Zamień na:"));
		ReplaceText->SetMaxLength(MAXINT);
		ReplaceText->SetSelection(0);
		KaiStaticText *repDescText = new KaiStaticText(this, -1, _("Zamień na:"), wxDefaultPosition, wxSize(-1/*80*/, -1));
		ReplaceStaticSizer->Add(repDescText, 1, wxALIGN_CENTER_VERTICAL /*| wxALIGN_RIGHT*/ | wxRIGHT, 4);
		ReplaceStaticSizer->Add(ReplaceText, 3, wxEXPAND, 0);
		mainfrbsizer1->Add(ReplaceStaticSizer, 0, wxEXPAND | wxALL, 4);
	}
	if (tabNum == WINDOW_FIND_IN_SUBS){
		//find in subs filters and path
		wxBoxSizer *SubsFilterStaticSizer = new wxBoxSizer(wxHORIZONTAL);
		FindInSubsPattern = new KaiChoice(this, ID_REPLACE_TEXT, L"", wxDefaultPosition, wxSize(276, -1), FR->subsFindingFilters);
		FindInSubsPattern->SetToolTip(_("Filtry wyszukiwania Windowsa pooddzielane średnikami np \"*.ass;*.srt\".\nZe względu na dużą wagę plików wideo\nszukanie wszystkiego \"*.*\" zostaje zmieniane na *.ass"));
		FindInSubsPattern->SetMaxLength(1000);
		FindInSubsPattern->SetSelection(0);
		SubsFilterStaticSizer->Add(new KaiStaticText(this, -1, _("Filtry:"), wxDefaultPosition,
			wxSize(-1/*80*/, -1)), 1, wxALIGN_CENTER_VERTICAL/* | wxALIGN_RIGHT*/ | wxRIGHT, 4);
		SubsFilterStaticSizer->Add(FindInSubsPattern, 3, wxEXPAND, 0);
		mainfrbsizer1->Add(SubsFilterStaticSizer, 0, wxEXPAND | wxALL, 4);

		wxBoxSizer *FindInSubsPathStaticSizer = new wxBoxSizer(wxHORIZONTAL);
		wxBoxSizer *FindInSubsPathAndChooseSizer = new wxBoxSizer(wxHORIZONTAL);
		FindInSubsPath = new KaiChoice(this, ID_REPLACE_TEXT, L"", wxDefaultPosition, wxSize(236, -1), FR->subsFindingPaths);
		FindInSubsPath->SetToolTip(_("Katalog szukania napisów:"));
		FindInSubsPath->SetMaxLength(MAXINT);
		FindInSubsPath->SetSelection(0);
		MappedButton *selectFolder = new MappedButton(this, 21345, L" ... ");
		Bind(wxEVT_COMMAND_BUTTON_CLICKED, [=](wxCommandEvent &evt){
			wxString destdir = wxDirSelector(_("Wybierz folder zapisu"), FindInSubsPath->GetValue(), 0, wxDefaultPosition, this);
			if (!destdir.empty())
				FindInSubsPath->SetValue(destdir);
		}, 21345);
		FindInSubsPathStaticSizer->Add(new KaiStaticText(this, -1, _("Katalog:"),
			wxDefaultPosition, wxSize(-1/*80*/, -1)), 1, wxALIGN_CENTER_VERTICAL/* | wxALIGN_RIGHT*/ | wxRIGHT, 4);
		FindInSubsPathAndChooseSizer->Add(FindInSubsPath, 3, wxEXPAND, 0);
		FindInSubsPathAndChooseSizer->Add(selectFolder, 0, wxEXPAND | wxLEFT, 4);
		FindInSubsPathStaticSizer->Add(FindInSubsPathAndChooseSizer, 3, wxEXPAND, 0);
		mainfrbsizer1->Add(FindInSubsPathStaticSizer, 0, wxEXPAND | wxALL, 4);
	}
	//checkboxes
	wxBoxSizer* frbsizer1 = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* checksSizer = new wxBoxSizer(wxVERTICAL);
	MatchCase = new KaiCheckBox(this, -1, _("Uwzględniaj wielkość liter"));
	MatchCase->SetValue((options & CASE_SENSITIVE) > 0);
	RegEx = new KaiCheckBox(this, -1, _("Wyrażenia regularne"));
	RegEx->SetValue((options & REG_EX) > 0);
	StartLine = new KaiCheckBox(this, ID_START_OF_LINE, _("Początek tekstu"));
	StartLine->SetValue((options & START_OF_TEXT) > 0);
	EndLine = new KaiCheckBox(this, ID_END_OF_LINE, _("Koniec tekstu"));
	EndLine->SetValue((options & END_OF_TEXT) > 0);
	frbsizer1->Add(MatchCase, 0, wxEXPAND | wxTOP | wxBOTTOM | wxLEFT, 2);
	frbsizer1->Add(RegEx, 0, wxEXPAND | wxTOP | wxBOTTOM | wxLEFT, 2);
	frbsizer1->Add(StartLine, 0, wxEXPAND | wxTOP | wxBOTTOM | wxLEFT, 2);
	frbsizer1->Add(EndLine, 0, wxEXPAND | wxTOP | wxBOTTOM | wxLEFT, 2);
	UseComments = new KaiCheckBox(this, -1, _("Uwzględnij komentarze"));
	UseComments->SetValue((options & SEEK_IN_COMMENTS) > 0);
	OnlyText = new KaiCheckBox(this, ID_ONLY_TEXT, _("Pomiń tagi"));
	OnlyText->SetValue((options & SEEK_ONLY_IN_TEXT) > 0);
	OnlyTags = new KaiCheckBox(this, ID_ONLY_TAGS, _("Pomiń tekst"));
	OnlyTags->SetValue((options & SEEK_ONLY_IN_TAGS) > 0);
	checksSizer->Add(UseComments, 0, wxEXPAND | wxTOP | wxBOTTOM | wxLEFT, 2);
	checksSizer->Add(OnlyText, 0, wxEXPAND | wxTOP | wxBOTTOM | wxLEFT, 2);
	checksSizer->Add(OnlyTags, 0, wxEXPAND | wxTOP | wxBOTTOM | wxLEFT, 2);
	Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, &TabWindow::OnRecheck, this, ID_START_OF_LINE, ID_ONLY_TAGS);
	//in field
	KaiStaticBoxSizer* frsbsizer2 = new KaiStaticBoxSizer(wxVERTICAL, this, _("W polu"));
	wxBoxSizer* frbsizer2 = new wxBoxSizer(wxHORIZONTAL);

	CollumnText = new KaiRadioButton(this, -1, _("Tekst"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	if (options & IN_FIELD_TEXT)
		CollumnText->SetValue(true);
	frbsizer2->Add(CollumnText, 1, wxEXPAND | wxALL, 2);

	CollumnStyle = new KaiRadioButton(this, -1, _("Styl"));
	if (options & IN_FIELD_STYLE)
		CollumnStyle->SetValue(true);
	frbsizer2->Add(CollumnStyle, 1, wxEXPAND | wxALL, 2);

	wxBoxSizer* frbsizer3 = new wxBoxSizer(wxHORIZONTAL);
	CollumnActor = new KaiRadioButton(this, -1, _("Aktor"));
	if (options & IN_FIELD_ACTOR)
		CollumnActor->SetValue(true);
	CollumnEffect = new KaiRadioButton(this, -1, _("Efekt"));
	if (options & IN_FIELD_EFFECT)
		CollumnEffect->SetValue(true);

	
	frbsizer3->Add(CollumnActor, 1, wxEXPAND | wxALL, 2);
	frbsizer3->Add(CollumnEffect, 1, wxEXPAND | wxALL, 2);

	//static box sizer adding first and second row
	frsbsizer2->Add(frbsizer2, 1, wxEXPAND | wxLEFT, 2);
	frsbsizer2->Add(frbsizer3, 1, wxEXPAND | wxLEFT, 2);
	//link checkboxes and in field radiobuttons
	mainfrbsizer2->Add(frbsizer1, 0, wxEXPAND, 0);
	mainfrbsizer2->Add(checksSizer, 0, wxEXPAND, 0);
	mainfrbsizer2->Add(frsbsizer2, 0, wxEXPAND, 0);

	mainfrbsizer1->Add(mainfrbsizer2, 0, wxEXPAND | wxLEFT, 1);
	//buttons
	//vertical sizer column 2
	wxBoxSizer* frbsizer = new wxBoxSizer(wxVERTICAL);
	if (tabNum != WINDOW_FIND_IN_SUBS){
		//find button
		MappedButton *ButtonFind = new MappedButton(this, ID_BUTTON_FIND, _("Znajdź"), -1, wxDefaultPosition, wxSize(150, -1));
		frbsizer->Add(ButtonFind, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, (tabNum == WINDOW_REPLACE) ? 2 : 4);

		Bind(wxEVT_COMMAND_BUTTON_CLICKED, [=](wxCommandEvent &evt){ FR->OnFind(this); }, ID_BUTTON_FIND);
	}
	if (tabNum == WINDOW_FIND){
		//find in all opened subs or in active subs buttons
		MappedButton *ButtonFindInAllOpenedSubs = new MappedButton(this, 
			ID_BUTTON_FIND_IN_ALL_OPENED_SUBS, _("Znajdź we wszystkich\notwartych napisach"), -1);
		MappedButton *ButtonFindAllInCurrentSubs = new MappedButton(this, 
			ID_BUTTON_FIND_ALL_IN_CURRENT_SUBS, _("Znajdź wszystko\nw bieżących napisach"), -1);

		frbsizer->Add(ButtonFindInAllOpenedSubs, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 4);
		frbsizer->Add(ButtonFindAllInCurrentSubs, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 4);

		Bind(wxEVT_COMMAND_BUTTON_CLICKED, [=](wxCommandEvent &evt){ 
			FR->FindInAllOpenedSubs(this); 
		}, ID_BUTTON_FIND_IN_ALL_OPENED_SUBS);
		Bind(wxEVT_COMMAND_BUTTON_CLICKED, [=](wxCommandEvent &evt){ 
			FR->FindAllInCurrentSubs(this); 
		}, ID_BUTTON_FIND_ALL_IN_CURRENT_SUBS);
	}
	else if (tabNum == WINDOW_REPLACE){
		//replace buttons
		MappedButton *ButtonReplaceNext = new MappedButton(this, ID_BUTTON_REPLACE, _("Zamień następne"), -1);
		MappedButton *ButtonReplaceAll = new MappedButton(this, ID_BUTTON_REPLACE_ALL, _("Zamień wszystko"), -1);
		MappedButton *ButtonReplaceOnAllTabs = new MappedButton(this, 
			ID_BUTTON_REPLACE_IN_ALL_OPENED_SUBS, _("Zamień we wszystkich\notwartych napisach"));

		frbsizer->Add(ButtonReplaceNext, 1, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 2);
		frbsizer->Add(ButtonReplaceAll, 1, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 2);
		frbsizer->Add(ButtonReplaceOnAllTabs, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 2);

		Bind(wxEVT_COMMAND_BUTTON_CLICKED, [=](wxCommandEvent &evt){ 
			FR->Replace(this); 
		}, ID_BUTTON_REPLACE);
		Bind(wxEVT_COMMAND_BUTTON_CLICKED, [=](wxCommandEvent &evt){ 
			FR->ReplaceAll(this); 
		}, ID_BUTTON_REPLACE_ALL);
		Bind(wxEVT_COMMAND_BUTTON_CLICKED, [=](wxCommandEvent &evt){ 
			FR->ReplaceInAllOpenedSubs(this); 
		}, ID_BUTTON_REPLACE_IN_ALL_OPENED_SUBS);
	}
	else if (tabNum == WINDOW_FIND_IN_SUBS){
		MappedButton *ButtonFindInSubs = new MappedButton(this, ID_BUTTON_FIND_IN_SUBS, 
			_("Znajdź w napisach"), -1, wxDefaultPosition, wxSize(150, -1));
		MappedButton *ButtonReplaceInSubs = new MappedButton(this, ID_BUTTON_REPLACE_IN_SUBS, 
			_("Zamień w napisach"), -1);

		frbsizer->Add(ButtonFindInSubs, 1, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 4);
		frbsizer->Add(ButtonReplaceInSubs, 1, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 4);

		Bind(wxEVT_COMMAND_BUTTON_CLICKED, [=](wxCommandEvent &evt){ 
			FR->FindInSubs(this); 
		}, ID_BUTTON_FIND_IN_SUBS);
		Bind(wxEVT_COMMAND_BUTTON_CLICKED, [=](wxCommandEvent &evt){ 
			FR->ReplaceInSubs(this); 
		}, ID_BUTTON_REPLACE_IN_SUBS);
	}

	MappedButton *ButtonClose = new MappedButton(this, ID_BUTTON_CLOSE, _("Zamknij"));
	frbsizer->Add(ButtonClose, 1, wxEXPAND | wxTOP | wxRIGHT, (tabNum == WINDOW_REPLACE) ? 2 : 4);
	Bind(wxEVT_COMMAND_BUTTON_CLICKED, [=](wxCommandEvent &evt){ FR->OnClose(); }, ID_BUTTON_CLOSE);

	if (tabNum == WINDOW_FIND_IN_SUBS){
		SeekInSubFolders = new KaiCheckBox(this, -1, _("Szukaj w podfolderach"));
		SeekInSubFolders->SetValue((options & SEARCH_SUBFOLDERS) > 0);
		SeekInHiddenFolders = new KaiCheckBox(this, -1, _("Szukaj\nw ukrytych folderach"));
		SeekInSubFolders->SetValue((options & SEARCH_HIDDEN_FOLDERS) > 0);

		frbsizer->Add(SeekInSubFolders, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 4);
		frbsizer->Add(SeekInHiddenFolders, 0, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, 4);
	}
	//łączenie całości znajdowania i opcji z przyciskami
	mainfrbsizer3->Add(mainfrbsizer1, 0, wxEXPAND | wxRIGHT, 2);
	long flags = wxEXPAND | wxLEFT;
	if (tabNum == WINDOW_REPLACE)
		flags |= wxRIGHT;
	mainfrbsizer3->Add(frbsizer, 1, flags, 2);
	mainfrbsizer->Add(mainfrbsizer3, 0, wxEXPAND | wxALL, 5);

	if (tabNum != WINDOW_FIND_IN_SUBS){
		//horizontal sizer bottom static box
		KaiStaticBoxSizer* frsbsizer3 = new KaiStaticBoxSizer(wxHORIZONTAL, this, _("Linijki"));

		AllLines = new KaiRadioButton(this, 23156, _("Wszystkie linijki"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
		if (options & IN_LINES_ALL)
			AllLines->SetValue(true);
		SelectedLines = new KaiRadioButton(this, 23157, _("Zaznaczone linijki"));
		if (options & IN_LINES_SELECTED)
			SelectedLines->SetValue(true);
		FromSelection = new KaiRadioButton(this, 23158, _("Od zaznaczonej"));
		if (options & IN_LINES_FROM_SELECTION)
			FromSelection->SetValue(true);

		Bind(wxEVT_COMMAND_RADIOBUTTON_SELECTED, &TabWindow::Reset, this, 23156);
		Bind(wxEVT_COMMAND_RADIOBUTTON_SELECTED, &TabWindow::Reset, this, 23157);
		Bind(wxEVT_COMMAND_RADIOBUTTON_SELECTED, &TabWindow::Reset, this, 23158);

		wxBoxSizer* frbsizer4 = new wxBoxSizer(wxHORIZONTAL);
		MappedButton *ButtonChooseStyle = new MappedButton(this, 
			ID_BUTTON_CHOOSE_STYLE, L"+", -1, wxDefaultPosition, wxDefaultSize, (long)MAKE_SQUARE_BUTTON);
		ChoosenStyleText = new KaiTextCtrl(this, ID_CHOOSEN_STYLE_TEXT, Options.GetString(FIND_REPLACE_STYLES));
		frbsizer4->Add(ButtonChooseStyle, 0, 0, 0);
		frbsizer4->Add(ChoosenStyleText, 0, wxLEFT, 3);

		frsbsizer3->Add(AllLines, 1, wxALL | wxEXPAND, 2);
		frsbsizer3->Add(SelectedLines, 1, wxALL | wxEXPAND, 2);
		frsbsizer3->Add(FromSelection, 1, wxALL | wxEXPAND, 2);
		frsbsizer3->Add(frbsizer4, 0, wxALL, 2);
		if (tabNum == WINDOW_FIND)
			mainfrbsizer->AddSpacer(2);

		mainfrbsizer->Add(frsbsizer3, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
		Bind(wxEVT_COMMAND_BUTTON_CLICKED, &TabWindow::OnStylesChoose, this, ID_BUTTON_CHOOSE_STYLE);
	}

	//setting sizera
	SetSizerAndFit(mainfrbsizer);

	//commands
	Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, &TabWindow::OnRecheck, this, ID_START_OF_LINE);
	Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, &TabWindow::OnRecheck, this, ID_END_OF_LINE);
}

void TabWindow::SaveValues()
{
	int options = 0;
	if (!AllLines){
		options = Options.GetInt(FIND_REPLACE_OPTIONS);
		options = options >> 9;
		options = options << 9;
	}

	if (MatchCase->GetValue())
		options |= CASE_SENSITIVE;
	if (RegEx->GetValue())
		options |= REG_EX;
	if (StartLine->GetValue())
		options |= START_OF_TEXT;
	if (EndLine->GetValue())
		options |= END_OF_TEXT;
	if (CollumnText->GetValue())
		options |= IN_FIELD_TEXT;
	if (CollumnStyle->GetValue())
		options |= IN_FIELD_STYLE;
	if (CollumnActor->GetValue())
		options |= IN_FIELD_ACTOR;
	if (CollumnEffect->GetValue())
		options |= IN_FIELD_EFFECT;
	if (AllLines && AllLines->GetValue())
		options |= IN_LINES_ALL;
	if (SelectedLines && SelectedLines->GetValue())
		options |= IN_LINES_SELECTED;
	if (FromSelection && FromSelection->GetValue())
		options |= IN_LINES_FROM_SELECTION;
	if (SeekInSubFolders && SeekInSubFolders->GetValue())
		options |= SEARCH_SUBFOLDERS;
	if (SeekInHiddenFolders && SeekInHiddenFolders->GetValue())
		options |= SEARCH_HIDDEN_FOLDERS;
	if (UseComments->GetValue())
		options |= SEEK_IN_COMMENTS;
	if (OnlyText->GetValue())
		options |= SEEK_ONLY_IN_TEXT;
	if (OnlyTags->GetValue())
		options |= SEEK_ONLY_IN_TAGS;

	Options.SetInt(FIND_REPLACE_OPTIONS, options);

	FR->actualFind = FindText->GetString(0);
	if (ReplaceText){
		FR->actualReplace = ReplaceText->GetString(0);
	}
	if (FindInSubsPattern){
		FR->actualFilters = FindInSubsPattern->GetString(0);
		FR->actualPaths = FindInSubsPath->GetString(0);
	}
	if (ChoosenStyleText){
		Options.SetString(FIND_REPLACE_STYLES, ChoosenStyleText->GetValue());
	}
}

void TabWindow::SetValues()
{
	int options = Options.GetInt(FIND_REPLACE_OPTIONS);
	MatchCase->SetValue((options & CASE_SENSITIVE) > 0);
	RegEx->SetValue((options & REG_EX) > 0);
	StartLine->SetValue((options & START_OF_TEXT) > 0);
	EndLine->SetValue((options & END_OF_TEXT) > 0);
	CollumnText->SetValue((options & IN_FIELD_TEXT) > 0);
	CollumnStyle->SetValue((options & IN_FIELD_STYLE) > 0);
	CollumnActor->SetValue((options & IN_FIELD_ACTOR) > 0);
	CollumnEffect->SetValue((options & IN_FIELD_EFFECT) > 0);

	if (AllLines)
		AllLines->SetValue((options & IN_LINES_ALL) > 0);
	if (SelectedLines)
		SelectedLines->SetValue((options & IN_LINES_SELECTED) > 0);
	if (FromSelection)
		FromSelection->SetValue((options & IN_LINES_FROM_SELECTION) > 0);

	UseComments->SetValue((options & SEEK_IN_COMMENTS) > 0);
	OnlyText->SetValue((options & SEEK_ONLY_IN_TEXT) > 0);
	OnlyTags->SetValue((options & SEEK_ONLY_IN_TAGS) > 0);

	FindText->SetValue(FR->actualFind);
	FindText->PutArray(&FR->findRecent);
	if (ReplaceText){
		ReplaceText->SetValue(FR->actualReplace);
		ReplaceText->PutArray(&FR->replaceRecent);
	}
	if (FindInSubsPattern){
		FindInSubsPattern->SetValue(FR->actualFilters);
		FindInSubsPattern->PutArray(&FR->subsFindingFilters);
		FindInSubsPath->SetValue(FR->actualPaths);
		FindInSubsPath->PutArray(&FR->subsFindingPaths);
	}
	if (ChoosenStyleText){
		ChoosenStyleText->SetValue(Options.GetString(FIND_REPLACE_STYLES));
	}
}

void TabWindow::OnRecheck(wxCommandEvent& event)
{
	int id = event.GetId();
	if (id == ID_START_OF_LINE && EndLine->GetValue()){
		EndLine->SetValue(false);
	}
	else if (id == ID_END_OF_LINE && StartLine->GetValue()){
		StartLine->SetValue(false);
	}
	else if (id == ID_ONLY_TEXT && OnlyText->GetValue()){
		OnlyTags->SetValue(false);
	}
	else if (id == ID_ONLY_TAGS && OnlyTags->GetValue()){
		OnlyText->SetValue(false);
	}

}

void TabWindow::Reset(wxCommandEvent& evt)
{
	FR->fromstart = true;
	FR->fnext = false;
}

void TabWindow::OnStylesChoose(wxCommandEvent& event)
{
	ChoosenStyleText->SetValue(GetCheckedElements(FR->Kai));
}

FindReplaceDialog::FindReplaceDialog(KainoteFrame *_Kai, int whichWindow)
	:KaiDialog(_Kai, -1, (whichWindow == WINDOW_FIND) ? _("Znajdź") : 
	(whichWindow == WINDOW_REPLACE) ? _("Znajdź i zamień") : _("Znajdź w napisach"))
	, Kai(_Kai)
{
	FR = new FindReplace(Kai, this);
	wxIcon icn;
	icn.CopyFromBitmap(CreateBitmapFromPngResource(L"SEARCH"));
	SetIcon(icn);
	SetForegroundColour(Options.GetColour(WINDOW_TEXT));
	SetBackgroundColour(Options.GetColour(WINDOW_BACKGROUND));

	DialogSizer *mainfrbsizer = new DialogSizer(wxVERTICAL);

	findReplaceTabs = new KaiTabBar(this, -1);
	//findReplaceTabs->SetFont(wxFont(13, wxSWISS, wxFONTSTYLE_NORMAL, wxNORMAL, false, L"Tahoma"));
	findReplaceTabs->AddTab(new TabWindow(findReplaceTabs, -1, WINDOW_FIND, FR), _("Znajdź"));
	findReplaceTabs->AddTab(new TabWindow(findReplaceTabs, -1, WINDOW_REPLACE, FR), _("Znajdź i zamień"));
	findReplaceTabs->AddTab(new TabWindow(findReplaceTabs, -1, WINDOW_FIND_IN_SUBS, FR), _("Znajdź w napisach"));
	findReplaceTabs->Fit();
	findReplaceTabs->Bind(BEFORE_CHANGING_TAB, [=](wxCommandEvent &evt){
		wxWindow *win = FindFocus();
		if (win)
			lastFocusedId = win->GetId();

		TabWindow *currentTab = GetTab();
		currentTab->SaveValues();
	});
	findReplaceTabs->Bind(TAB_CHANGED, [=](wxCommandEvent &evt){
		TabWindow *currentTab = GetTab();
		currentTab->SetValues();
		SetLabel(findReplaceTabs->GetTabName());
		SetSelection(currentTab);
	});
	mainfrbsizer->Add(findReplaceTabs, 0, wxEXPAND | wxALL, 2);
	SetSizerAndFit(mainfrbsizer);

	Bind(wxEVT_ACTIVATE, &FindReplaceDialog::OnActivate, this);
	//Connect(ID_BCLOSE, wxEVT_COMMAND_BUTTON_CLICKED, (wxObjectEventFunction)&FindReplace::OnClose);
	Bind(wxEVT_COMMAND_BUTTON_CLICKED, &FindReplaceDialog::OnEnterConfirm, this, ID_ENTER_CONFIRM);
	SetEscapeId(ID_BUTTON_CLOSE);
	SetEnterId(ID_ENTER_CONFIRM);
	CenterOnParent();

	wxAcceleratorEntry entries[2];
	entries[0] = Hkeys.GetHKey(idAndType(GLOBAL_SEARCH));
	entries[1] = Hkeys.GetHKey(idAndType(GLOBAL_FIND_REPLACE));
	wxAcceleratorTable accel(2, entries);
	SetAcceleratorTable(accel);
	Bind(wxEVT_COMMAND_MENU_SELECTED, [=](wxCommandEvent &evt){
		TabWindow *currentTab = GetTab();
		if (currentTab->windowType != WINDOW_FIND){ findReplaceTabs->SetTab(0); }
		else{ Hide(); }
	}, GLOBAL_SEARCH);
	Bind(wxEVT_COMMAND_MENU_SELECTED, [=](wxCommandEvent &evt){
		TabWindow *currentTab = GetTab();
		if (currentTab->windowType != WINDOW_REPLACE){ findReplaceTabs->SetTab(1); }
		else{ Hide(); }
	}, GLOBAL_FIND_REPLACE);
	if (whichWindow != WINDOW_FIND)
		findReplaceTabs->SetTab(whichWindow);

	Show();
	SetSelection(GetTab());
}

FindReplaceDialog::~FindReplaceDialog()
{
	delete FR;
}

void FindReplaceDialog::ShowDialog(int whichWindow)
{
	TabWindow *currentTab = GetTab();
	if (currentTab->windowType == whichWindow)
		Show(false);

	findReplaceTabs->SetTab(whichWindow);
	if (!IsShown())
		Show();

	SetSelection(GetTab());
}

void FindReplaceDialog::SaveOptions()
{
	TabWindow *currentTab = GetTab();
	currentTab->SaveValues();
}

void FindReplaceDialog::OnActivate(wxActivateEvent& event)
{
	if (!event.GetActive() || FR->blockTextChange){
		if (event.GetActive()){ FR->blockTextChange = false; } return;
	}
	TabWindow *currentTab = GetTab();
	long from, to, fromO, toO;
	TabPanel *tab = Kai->GetTab();
	EditBox *edit = tab->Edit;
	edit->TextEdit->GetSelection(&from, &to);
	edit->TextEditOrig->GetSelection(&fromO, &toO);
	KaiChoice * findOrReplace = currentTab->FindText;
	if (from < to){
		if (from == FR->findstart && to == FR->findend)
			return;
		wxString selected = edit->TextEdit->GetValue().SubString(from, to - 1);
		if (selected.Lower() != findOrReplace->GetValue().Lower()){ findOrReplace->SetValue(selected); }
		//when someone changed selection, then restore textposition to 0 maybe restore lineposition too? It's different seeking
		FR->textPosition = FR->linePosition = 0;
	}
	else if (fromO < toO){
		if (fromO == FR->findstart && toO == FR->findend)
			return;
		wxString selected = edit->TextEditOrig->GetValue().SubString(fromO, toO - 1);
		if (selected.Lower() != findOrReplace->GetValue().Lower()){ findOrReplace->SetValue(selected); }
		FR->textPosition = FR->linePosition = 0;
	}
	findOrReplace->SetFocus();

	FR->findTextReset = false;
}

void FindReplaceDialog::OnEnterConfirm(wxCommandEvent& event)
{
	TabWindow *currentTab = GetTab();
	if (currentTab->windowType){
		if (currentTab->windowType == WINDOW_FIND_IN_SUBS && currentTab->ReplaceText->HasFocus()){
			FR->ReplaceInSubs(currentTab);
			return;
		}
		if (currentTab->ReplaceText->HasFocus()){
			FR->Replace(currentTab);
			return;
		}
	}
	if (currentTab->windowType == WINDOW_FIND_IN_SUBS)
		FR->FindInSubs(currentTab);
	else{
		FR->Find(currentTab);
		FR->fnext = false;
	}
}

void FindReplaceDialog::SetSelection(TabWindow *tab)
{
	if (lastFocusedId == ID_REPLACE_TEXT && tab->ReplaceText){
		//tab->ReplaceText->choiceText->SetSelection(0, -1);
		tab->ReplaceText->SetFocus();
	}
	else{
		//tab->FindText->choiceText->SetSelection(0, -1);
		tab->FindText->SetFocus();
	}
}

TabWindow * FindReplaceDialog::GetTab()
{
	return (TabWindow *)findReplaceTabs->GetTab();
}

void FindReplaceDialog::FindNext()
{
	if (!FR->findString.empty()) {
		FR->Find(NULL);
	}
}

void FindReplaceDialog::Reset()
{
	FR->fromstart = true;
	FR->fnext = false;
}