// Copyright 2010 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ========================================================================

#include "omaha/ui/splash_screen.h"
#include "base/basictypes.h"
#include "omaha/base/app_util.h"
#include "omaha/base/constants.h"
#include "omaha/base/const_object_names.h"
#include "omaha/base/debug.h"
#include "omaha/base/error.h"
#include "omaha/base/logging.h"
#include "omaha/base/smart_handle.h"
#include "omaha/base/utils.h"
#include "omaha/base/window_utils.h"
#include "omaha/client/client_utils.h"
#include "omaha/client/resource.h"
#include "omaha/common/lang.h"
#include "omaha/google_update/resource.h"   // For the IDI_APP
#include "omaha/ui/scoped_gdi.h"

namespace {

const int kClosingTimerID = 1;
// Frequency that the window changes alpah blending value during fading stage.
const int kTimerInterval = 100;

// Alpha blending values for the fading effect.
const int kDefaultAlphaScale = 100;
const int kAlphaScales[] = { 0, 30, 47, 62, 75, 85, 93, kDefaultAlphaScale };

uint8 AlphaScaleToAlphaValue(int alpha_scale) {
  ASSERT1(alpha_scale >= 0 && alpha_scale <= 100);
  return static_cast<uint8>(alpha_scale * 255 / 100);
}

}  // namespace

namespace omaha {

SplashScreen::SplashScreen(const CString& bundle_name)
    : IDD(IDD_PROGRESS),
      alpha_index_(0),
      timer_created_(false) {
  CORE_LOG(L3, (_T("[SplashScreen::SplashScreen]")));
  caption_ = client_utils::GetInstallerDisplayName(bundle_name);
  text_.FormatMessage(IDS_SPLASH_SCREEN_MESSAGE, caption_);

  SwitchToState(STATE_CREATED);
}

SplashScreen::~SplashScreen() {
  CORE_LOG(L3, (_T("[SplashScreen::~SplashScreen]")));

  const int kWaitTimeoutInMillisecond = 60000;

  // Before the object goes out of scope, waits the thread to exit to avoid
  // it accessing the object after that.
  if (thread_.Running() && !thread_.WaitTillExit(kWaitTimeoutInMillisecond)) {
    CORE_LOG(LW, (_T("[SplashScreen: thread failed to exit gracefully]")));
    return;
  }

  ASSERT1(state_ == STATE_CREATED || state_ == STATE_CLOSED);
}

void SplashScreen::Show() {
  AutoSync get_lock(lock_);

  if (state_ == STATE_CREATED) {
    thread_.Start(this);
  } else {
    ASSERT1(false);
  }
}

void SplashScreen::Dismiss() {
  AutoSync get_lock(lock_);

  switch (state_) {
    case STATE_CREATED:
      SwitchToState(STATE_CLOSED);
      break;

    case STATE_SHOW_NORMAL:
      SwitchToState(STATE_FADING);
      break;

    case STATE_CLOSED:
    case STATE_FADING:
    case STATE_INITIALIZED:
      break;

    default:
      ASSERT1(false);
      break;
  }
}

HRESULT SplashScreen::Initialize() {
  CORE_LOG(L3, (_T("[SplashScreen::Initialize]")));

  ASSERT1(!IsWindow());
  ASSERT1(state_ == STATE_CREATED);

  if (!Create(NULL)) {
    return GOOPDATE_E_UI_INTERNAL_ERROR;
  }

  VERIFY1(SetWindowText(caption_));

  EnableSystemButtons(false);
  GetDlgItem(IDC_IMAGE).ShowWindow(SW_HIDE);

  CWindow text_wnd = GetDlgItem(IDC_INSTALLER_STATE_TEXT);
  text_wnd.ShowWindow(SW_SHOWNORMAL);
  text_wnd.SetWindowText(text_);

  InitProgressBar();

  ::SetLayeredWindowAttributes(
      m_hWnd,
      0,
      AlphaScaleToAlphaValue(kDefaultAlphaScale),
      LWA_ALPHA);

  VERIFY1(CenterWindow(NULL));
  HRESULT hr = WindowUtils::SetWindowIcon(m_hWnd, IDI_APP, address(hicon_));
  if (FAILED(hr)) {
    CORE_LOG(LW, (_T("[SetWindowIcon failed][0x%08x]"), hr));
  }
  SwitchToState(STATE_INITIALIZED);
  return S_OK;
}

void SplashScreen::EnableSystemButtons(bool enable) {
  const LONG kSysStyleMask = WS_MINIMIZEBOX | WS_SYSMENU | WS_MAXIMIZEBOX;

  if (enable) {
    SetWindowLong(GWL_STYLE, GetWindowLong(GWL_STYLE) | kSysStyleMask);
  } else {
    SetWindowLong(GWL_STYLE, GetWindowLong(GWL_STYLE) & ~kSysStyleMask);
  }
}

void SplashScreen::InitProgressBar() {
  const LONG kStyle = WS_CHILD | WS_VISIBLE | PBS_MARQUEE | PBS_SMOOTH;

  CWindow progress_bar = GetDlgItem(IDC_PROGRESS);
  LONG style = progress_bar.GetWindowLong(GWL_STYLE) | kStyle;
  progress_bar.SetWindowLong(GWL_STYLE, style);
  progress_bar.SendMessage(PBM_SETMARQUEE, TRUE, 60);
}

LRESULT SplashScreen::OnTimer(UINT message,
                              WPARAM wparam,
                              LPARAM lparam,
                              BOOL& handled) {
  UNREFERENCED_PARAMETER(message);
  UNREFERENCED_PARAMETER(wparam);
  UNREFERENCED_PARAMETER(lparam);

  ASSERT1(state_ == STATE_FADING);
  ASSERT1(alpha_index_ > 0);
  if (--alpha_index_) {
    ::SetLayeredWindowAttributes(
        m_hWnd,
        0,
        AlphaScaleToAlphaValue(kAlphaScales[alpha_index_]),
        LWA_ALPHA);
  } else {
    Close();
  }

  handled = TRUE;
  return 0;
}

LRESULT SplashScreen::OnClose(UINT message,
                              WPARAM wparam,
                              LPARAM lparam,
                              BOOL& handled) {
  UNREFERENCED_PARAMETER(message);
  UNREFERENCED_PARAMETER(wparam);
  UNREFERENCED_PARAMETER(lparam);

  DestroyWindow();
  handled = TRUE;
  return 0;
}

LRESULT SplashScreen::OnDestroy(UINT message,
                                WPARAM wparam,
                                LPARAM lparam,
                                BOOL& handled) {
  UNREFERENCED_PARAMETER(message);
  UNREFERENCED_PARAMETER(wparam);
  UNREFERENCED_PARAMETER(lparam);

  if (timer_created_) {
    ASSERT1(IsWindow());
    KillTimer(kClosingTimerID);
  }

  ::PostQuitMessage(0);

  handled = TRUE;
  return 0;
}

void SplashScreen::SwitchToState(WindowState new_state) {
  AutoSync get_lock(lock_);

  state_ = new_state;
  switch (new_state) {
    case STATE_CREATED:
    case STATE_INITIALIZED:
      break;
    case STATE_SHOW_NORMAL:
      alpha_index_ = arraysize(kAlphaScales) - 1;
      break;
    case STATE_FADING:
      ASSERT1(IsWindow());
      timer_created_ = (SetTimer(kClosingTimerID, kTimerInterval, NULL) != 0);
      if (!timer_created_) {
        CORE_LOG(LW,
                 (_T("[SetTimer failed, closing window directly.][0x%08x]"),
                  HRESULTFromLastError()));
        Close();
      }
      break;
    case STATE_CLOSED:
      break;
    default:
      ASSERT1(false);
      break;
  }
}

void SplashScreen::Run() {
  {
    AutoSync get_lock(lock_);

    if (state_ != STATE_CREATED) {
      return;
    }

    // Initialize() has to be called in this thread so that it is the owner of
    // the window and window messages can be correctly routed by the message
    // loop.
    if (FAILED(Initialize())) {
      return;
    }

    ASSERT1(IsWindow());
    ShowWindow(SW_SHOWNORMAL);
    SwitchToState(STATE_SHOW_NORMAL);
  }

  CMessageLoop message_loop;
  message_loop.Run();

  SwitchToState(STATE_CLOSED);
}

void SplashScreen::Close() {
  AutoSync get_lock(lock_);

  if (state_ != STATE_CLOSED && IsWindow()) {
    PostMessage(WM_CLOSE, 0, 0);
  }
}

}  // namespace omaha
