/***************************************************************************
  GET Delay MFD page implementation.

  Buttons (left column of the MFD, top to bottom; keys in brackets):
    SIM [S] mark simulation   TRN [T] mark transcript
    APL [A] apply candidate   SET [M] type an ABSOLUTE offset as [+-]M:SS
    NEG [N] flip the sign of the candidate
    CLR [C] clear applied offset, event marks and candidate
  Event marks stage a CORRECTION that APL ADDS to the applied offset; manual
  SET entry stages an absolute offset that APL replaces it with.
  ***************************************************************************/

#pragma include_alias( <fstream.h>, <fstream> )
#include "Orbitersdk.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "GroundElapsedTimeCommon.h"
#include "GroundElapsedTimeOffset.h"
#include "GroundElapsedTimeMFD.h"

static int g_mode = 0;

static const MFDBUTTONMENU g_menu[] = {
	{ "Sim event", "occurred", 'S' },
	{ "Transcript event", "heard", 'T' },
	{ "Apply candidate", "to GET output", 'A' },
	{ "Set absolute offset", "M:SS (+ adds)", 'M' },
	{ "Flip sign of", "candidate", 'N' },
	{ "Clear offset", "and event marks", 'C' },
};
static const char *g_labels[] = { "SIM", "TRN", "APL", "SET", "NEG", "CLR" };
static const int NUM_BUTTONS = sizeof(g_labels) / sizeof(g_labels[0]);

static char g_status[96] = "";

static void SetStatus(const char *s)
{
	strncpy(g_status, s, sizeof(g_status) - 1);
	g_status[sizeof(g_status) - 1] = '\0';
}

// The open MFD page (if any), so the input-box callback can request a redraw.
// Cleared in the destructor, so the callback is safe if the MFD closes first.
static class GroundElapsedTimeMFD *g_open = 0;

// Input-box callback. Updates module state and asks the open page (if any)
// to redraw immediately, so the staged value shows before APL is pressed.
static void RequestRedraw();

static bool DelayEntered(void *id, char *str, void *data)
{
	long seconds;
	if (!GetOffsetParse(str, seconds))
	{
		SetStatus("Invalid delay. Use [+-]M:SS, e.g. +2:30");
		return false;
	}
	GetOffsetSetCandidate(seconds);
	char t[48], m[96];
	GetOffsetFormat(GetOffsetCandidate(), t, sizeof(t));
	sprintf(m, "Manual absolute %s staged - press APL to REPLACE applied offset", t);
	SetStatus(m);
	RequestRedraw();
	return true;
}

class GroundElapsedTimeMFD : public MFD2
{
public:
	GroundElapsedTimeMFD(DWORD w, DWORD h, VESSEL *vessel) : MFD2(w, h, vessel) { g_open = this; }
	~GroundElapsedTimeMFD() { if (g_open == this) g_open = 0; }
	void Redraw() { InvalidateDisplay(); }

	struct Grid;

	bool Update(oapi::Sketchpad *sk)
	{
		Title(sk, "GET Delay");
		sk->SetFont(GetDefaultFont(0));
		sk->SetTextAlign(oapi::Sketchpad::LEFT, oapi::Sketchpad::TOP);
		int x = (int)(W / 16);
		int lh = (int)HIWORD(sk->GetCharSize());
		if (lh < 8) lh = 14;
		// Fixed row grid, like the Project Apollo MFDs: every text row (wrapped
		// continuation rows included) is followed by one blank row.
		int pitch = lh * 2;
		int y = lh * 3;
		Grid g = { sk, x, y, pitch, lh, true };
		Content(g, true);
		// Explanatory formula lines are the only content dropped, and only when
		// everything would not fit the MFD height.
		bool full = g.y - pitch + lh <= (int)H - lh;
		g.y = y;
		g.dry = false;
		Content(g, full);
		return true;
	}

	void Content(Grid &gr, bool full)
	{
		Grid *skp = &gr;
		int x = gr.x, y = gr.y;
		char b[128], t[48];
		double raw = 0.0;
		VESSEL *v = oapiGetFocusInterface();
		bool haveGet = v && ComputeGroundElapsedTime(v, raw);

		skp->SetTextColor(RGB(255, 255, 255));
		if (haveGet)
		{
			FormatGroundElapsedTimeHMS(raw, t, sizeof(t));
			sprintf(b, "Raw GET:     %s", t);
			Line(skp, x, y, b);
			FormatGroundElapsedTimeHMS(GetOffsetApplyToGET(raw), t, sizeof(t));
			sprintf(b, "Output GET:  %s", t);
			Line(skp, x, y, b);
		}
		else
		{
			Line(skp, x, y, "Raw GET:     unsupported vessel");
			Line(skp, x, y, "Output GET:  0:00:00 (no offset)");
		}

		GetOffsetFormat(GetOffsetApplied(), t, sizeof(t));
		skp->SetTextColor(GetOffsetApplied() ? RGB(255, 255, 0) : RGB(0, 255, 0));
		sprintf(b, "Applied offset: %s %s", t, GetOffsetApplied() ? "" : "(none)");
		Line(skp, x, y, b);
		if (full)
		{
			skp->SetTextColor(RGB(200, 200, 200));
			Line(skp, x, y, GetOffsetApplied() > 0 ? "  output = raw GET + offset" :
				GetOffsetApplied() < 0 ? "  output = raw GET - offset" : "  output = raw GET");
		}

		skp->SetTextColor(GetOffsetHasSimEvent() ? RGB(0, 255, 0) : RGB(160, 160, 160));
		Line(skp, x, y, GetOffsetHasSimEvent() ? "SIM event: MARKED" : "SIM event: not marked");
		skp->SetTextColor(GetOffsetHasTranscriptEvent() ? RGB(0, 255, 0) : RGB(160, 160, 160));
		Line(skp, x, y, GetOffsetHasTranscriptEvent() ? "Transcript heard: MARKED" : "Transcript heard: not marked");

		skp->SetTextColor(RGB(255, 255, 255));
		if (GetOffsetHasCandidate())
		{
			bool applied = GetOffsetCandidateApplied();
			bool corr = GetOffsetCandidateIsCorrection();
			GetOffsetFormat(GetOffsetCandidate(), t, sizeof(t));
			sprintf(b, "%s: %s", corr ? "Measured correction" : "Manual absolute", t);
			Line(skp, x, y, b);
			GetOffsetFormat(GetOffsetResulting(), t, sizeof(t));
			sprintf(b, "%s offset: %s", applied ? "New applied" : "Resulting", t);
			Line(skp, x, y, b);
			skp->SetTextColor(applied ? RGB(0, 255, 0) : RGB(255, 128, 0));
			if (applied)
				Line(skp, x, y, corr ? "  APPLIED (added to previous offset)" : "  APPLIED (replaced offset)");
			else
				Line(skp, x, y, corr ? "  NOT APPLIED - APL adds correction" : "  NOT APPLIED - APL replaces offset");
		}
		else
			Line(skp, x, y, "Correction/candidate: none");
		skp->SetTextColor(RGB(200, 200, 200));
		if (full)
			Line(skp, x, y, "Output GET = raw GET + offset");
		if (GetOffsetHasSimEvent() != GetOffsetHasTranscriptEvent())
			Line(skp, x, y, "Mark the other event to calculate a correction");
		if (g_status[0])
		{
			skp->SetTextColor(RGB(255, 255, 0));
			Line(skp, x, y, g_status);
		}
	}

	int ButtonMenu(const MFDBUTTONMENU **menu) const
	{
		if (menu) *menu = g_menu;
		return NUM_BUTTONS;
	}

	char *ButtonLabel(int bt)
	{
		return (bt >= 0 && bt < NUM_BUTTONS) ? (char *)g_labels[bt] : 0;
	}

	bool ConsumeButton(int bt, int event)
	{
		if (!(event & PANEL_MOUSE_LBDOWN))
			return false;
		return Action(bt);
	}

	bool ConsumeKeyBuffered(DWORD key)
	{
		switch (key)
		{
		case OAPI_KEY_S: return Action(0);
		case OAPI_KEY_T: return Action(1);
		case OAPI_KEY_A: return Action(2);
		case OAPI_KEY_M: return Action(3);
		case OAPI_KEY_N: return Action(4);
		case OAPI_KEY_C: return Action(5);
		}
		return false;
	}

private:
	// Text grid: every text row is followed by one blank row (pitch = 2 x font
	// height). Long text wraps onto following rows. A line that does not fit
	// entirely above the bottom margin is skipped, never clipped.
	struct Grid
	{
		oapi::Sketchpad *sk;
		int x, y, pitch, lh;
		bool dry; // measure only: advance y without drawing or skipping
		void SetTextColor(COLORREF c) { sk->SetTextColor(c); }
	};

	void Line(Grid *g, int, int &, const char *s)
	{
		oapi::Sketchpad *skp = g->sk;
		int cw = (int)LOWORD(skp->GetCharSize());
		int maxw = (int)W - 2 * g->x;
		int indent = 0;
		while (s[0] == ' ') { indent++; s++; }
		int ix = indent * cw;

		// Break into rows first so the whole line can be checked against the
		// bottom margin.
		const char *start[8];
		int count[8], rows = 0;
		while (*s && rows < 8)
		{
			int len = (int)strlen(s), fit = 0, brk = 0;
			while (fit < len && skp->GetTextWidth(s, fit + 1) <= maxw - ix - (rows ? 2 * cw : 0))
			{
				fit++;
				if (s[fit] == ' ' || s[fit] == '\0') brk = fit;
			}
			if (fit == len) brk = len;
			else if (brk == 0) brk = fit > 0 ? fit : 1;
			start[rows] = s;
			count[rows++] = brk;
			s += brk;
			while (*s == ' ') s++;
		}
		if (g->dry) { g->y += rows * g->pitch; return; }
		if (g->y + (rows - 1) * g->pitch + g->lh > (int)H - g->lh) return;
		for (int i = 0; i < rows; i++)
		{
			skp->Text(g->x + ix + (i ? 2 * cw : 0), g->y, start[i], count[i]);
			g->y += g->pitch;
		}
	}

	bool Action(int bt)
	{
		ULONGLONG now = GetTickCount64();
		switch (bt)
		{
		case 0:
			GetOffsetMarkSimEvent(now);
			SetStatus(GetOffsetHasTranscriptEvent()
				? "SIM recaptured; correction recalculated - press APL"
				: "SIM event marked; mark TRN when transcript event is heard");
			break;
		case 1:
			GetOffsetMarkTranscriptEvent(now);
			SetStatus(GetOffsetHasSimEvent()
				? "TRN recaptured; correction recalculated - press APL"
				: "Transcript event marked; mark SIM for the matching simulation event");
			break;
		case 2:
			SetStatus(GetOffsetApplyCandidate() ? "Applied; marks cleared for next measurement" : "Nothing to apply (no or already applied candidate)");
			break;
		case 3:
		{
			static char title[] = "GET delay [+-]M:SS (+ adds to raw GET):";
			oapiOpenInputBox(title, DelayEntered, 0, 20, 0);
			SetStatus("");
			break;
		}
		case 4:
			SetStatus(GetOffsetFlipCandidate() ? "Candidate sign flipped - press APL" : "No candidate to flip");
			break;
		case 5:
			GetOffsetReset();
			SetStatus("Offset, candidate and event marks cleared");
			break;
		default:
			return false;
		}
		InvalidateDisplay();
		return true;
	}
};

static void RequestRedraw()
{
	if (g_open) g_open->Redraw();
}

static OAPI_MSGTYPE MsgProc(UINT msg, UINT mfd, WPARAM wparam, LPARAM lparam)
{
	if (msg == OAPI_MSG_MFD_OPENED)
		return (OAPI_MSGTYPE)new GroundElapsedTimeMFD(LOWORD(wparam), HIWORD(wparam), (VESSEL *)lparam);
	return 0;
}

void GroundElapsedTimeMFDInit(HINSTANCE hDLL)
{
	static char name[] = "GET Delay";
	MFDMODESPECEX spec;
	spec.name = name;
	spec.key = OAPI_KEY_G;
	spec.context = NULL;
	spec.msgproc = MsgProc;
	g_mode = oapiRegisterMFDMode(spec);
}

void GroundElapsedTimeMFDExit()
{
	if (g_mode)
		oapiUnregisterMFDMode(g_mode);
	g_mode = 0;
}
