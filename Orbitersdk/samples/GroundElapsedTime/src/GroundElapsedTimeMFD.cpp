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

// Input-box callback. It only touches module state (never the MFD object),
// so it stays valid even if the MFD is closed while the box is open.
static bool DelayEntered(void *id, char *str, void *data)
{
	long seconds;
	if (!GetOffsetParse(str, seconds))
	{
		SetStatus("Invalid delay. Use [+-]M:SS, e.g. +2:30");
		return false;
	}
	GetOffsetSetCandidate(seconds);
	SetStatus("Absolute offset staged - press APL (replaces applied)");
	return true;
}

class GroundElapsedTimeMFD : public MFD2
{
public:
	GroundElapsedTimeMFD(DWORD w, DWORD h, VESSEL *vessel) : MFD2(w, h, vessel) {}

	bool Update(oapi::Sketchpad *skp)
	{
		Title(skp, "GET Delay");
		skp->SetFont(GetDefaultFont(0));
		skp->SetTextAlign(oapi::Sketchpad::LEFT, oapi::Sketchpad::TOP);
		int x = (int)(W / 16);
		int lh = (int)HIWORD(skp->GetCharSize());
		if (lh < 8) lh = 14;
		int y = lh * 2;

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
		y += lh / 2;

		GetOffsetFormat(GetOffsetApplied(), t, sizeof(t));
		skp->SetTextColor(GetOffsetApplied() ? RGB(255, 255, 0) : RGB(0, 255, 0));
		sprintf(b, "Applied offset: %s %s", t, GetOffsetApplied() ? "" : "(none)");
		Line(skp, x, y, b);
		skp->SetTextColor(RGB(200, 200, 200));
		Line(skp, x, y, GetOffsetApplied() > 0 ? "  output = raw GET + offset" :
			GetOffsetApplied() < 0 ? "  output = raw GET - offset" : "  output = raw GET");
		y += lh / 2;

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
		y += lh / 2;
		skp->SetTextColor(RGB(200, 200, 200));
		Line(skp, x, y, "Output GET = raw GET + offset");
		if (GetOffsetHasSimEvent() != GetOffsetHasTranscriptEvent())
			Line(skp, x, y, "Mark the other event to calculate a correction");
		if (g_status[0])
		{
			skp->SetTextColor(RGB(255, 255, 0));
			Line(skp, x, y, g_status);
		}
		return true;
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
	void Line(oapi::Sketchpad *skp, int x, int &y, const char *s)
	{
		skp->Text(x, y, s, (int)strlen(s));
		int lh = (int)HIWORD(skp->GetCharSize());
		y += (lh < 8 ? 14 : lh) + 2;
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
