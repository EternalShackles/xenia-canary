/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstring>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/string_util.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/user_profile.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xenumerator.h"
#include "xenia/kernel/xsession.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

DECLARE_int32(user_language);

DECLARE_int32(user_country);

namespace xe {
namespace kernel {
namespace xam {

// XUserGetXUID = XamUserGetXUID(user_index, X_USER_XUID_OFFLINE |
// X_USER_XUID_ONLINE | X_USER_XUID_GUEST, xuid_ptr)
X_HRESULT_result_t XamUserGetXUID_entry(dword_t user_index, dword_t type_mask,
                                        lpqword_t xuid_ptr) {
  assert_true(type_mask == 1 || type_mask == 2 || type_mask == 3 ||
              type_mask == 4 || type_mask == 7);
  if (!xuid_ptr) {
    return X_E_INVALIDARG;
  }

  *xuid_ptr = 0;

  if (user_index >= XUserMaxUserCount) {
    return X_E_INVALIDARG;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_NO_SUCH_USER;
  }

  const auto& user_profile =
      kernel_state()->xam_state()->GetUserProfile(user_index);

  uint32_t result = X_E_NO_SUCH_USER;
  uint64_t xuid = 0;

  if ((type_mask & X_USER_XUID_ONLINE)) {
    xuid = user_profile->GetLogonXUID();
    result = X_E_SUCCESS;
  } else if ((type_mask & X_USER_XUID_OFFLINE)) {
    xuid = user_profile->xuid();
    result = X_E_SUCCESS;
  }

  if (type_mask == X_USER_XUID_GUEST) {
    result = X_E_NO_SUCH_USER;
  }

  *xuid_ptr = xuid;
  return result;
}
DECLARE_XAM_EXPORT1(XamUserGetXUID, kUserProfiles, kImplemented);

dword_result_t XamUserGetIndexFromXUID_entry(qword_t xuid, dword_t flags,
                                             pointer_t<uint32_t> index) {
  if (!index) {
    return X_E_INVALIDARG;
  }

  const uint8_t user_index =
      kernel_state()->xam_state()->GetUserIndexAssignedToProfileFromXUID(xuid);

  if (user_index == XUserIndexAny) {
    return X_E_NO_SUCH_USER;
  }

  *index = user_index;

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserGetIndexFromXUID, kUserProfiles, kImplemented);

dword_result_t XamUserGetSigninState_entry(dword_t user_index) {
  // Yield, as some games spam this.
  xe::threading::MaybeYield();
  X_USER_SIGNIN_STATE signin_state = X_USER_SIGNIN_STATE::NotSignedIn;
  if (user_index >= XUserMaxUserCount) {
    return static_cast<uint32_t>(signin_state);
  }

  if (kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    const auto& user_profile =
        kernel_state()->xam_state()->GetUserProfile(user_index);
    signin_state = user_profile->signin_state();
  }

  return static_cast<uint32_t>(signin_state);
}
DECLARE_XAM_EXPORT2(XamUserGetSigninState, kUserProfiles, kImplemented,
                    kHighFrequency);

typedef struct {
  xe::be<uint64_t> xuid;
  xe::be<uint32_t> flags;
  xe::be<uint32_t> signin_state;
  xe::be<uint32_t> guest_num;
  xe::be<uint32_t> user_index;
  char name[16];
} X_USER_SIGNIN_INFO;
static_assert_size(X_USER_SIGNIN_INFO, 40);

X_HRESULT_result_t XamUserGetSigninInfo_entry(
    dword_t user_index, dword_t flags, pointer_t<X_USER_SIGNIN_INFO> info_ptr) {
  if (!info_ptr) {
    return X_E_INVALIDARG;
  }

  info_ptr.Zero();

  if (user_index >= XUserMaxUserCount) {
    return X_E_NO_SUCH_USER;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_E_NO_SUCH_USER;
  }

  const auto& user_profile =
      kernel_state()->xam_state()->GetUserProfile(user_index);

  xe::string_util::copy_truncating(info_ptr->name, user_profile->name(),
                                   xe::countof(info_ptr->name));

  if (user_profile->IsLiveEnabled()) {
    info_ptr->flags |= X_USER_INFO_FLAG_LIVE_ENABLED;
  }

  // 4D530910 has savefile issues
  // 434D0849 expects XUID for XUserReadStats when flags == 0
  // 415608CB joins systemlink session twice
  if (!flags || flags & X_USER_GET_SIGNIN_INFO_ONLINE_XUID_ONLY) {
    info_ptr->xuid = user_profile->GetLogonXUID();
  }

  // Contradictions:
  // 41560817 uses offline XUID to create sessions
  // 4D530919 uses offline XUID to join sessions
  // 5841125A uses offline XUID to show gamer card
  if (flags & X_USER_GET_SIGNIN_INFO_OFFLINE_XUID_ONLY) {
    info_ptr->xuid = user_profile->GetLogonXUID();
  }

  info_ptr->signin_state = static_cast<uint32_t>(user_profile->signin_state());

  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserGetSigninInfo, kUserProfiles, kImplemented);

dword_result_t XamUserGetName_entry(dword_t user_index, dword_t buffer,
                                    dword_t buffer_len) {
  if (user_index >= XUserMaxUserCount) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    // Based on XAM only first byte is cleared in case of lack of user.
    kernel_memory()->Zero(buffer, 1);
    return X_ERROR_NO_SUCH_USER;
  }

  const auto& user_profile =
      kernel_state()->xam_state()->GetUserProfile(user_index);

  // Because name is always limited to 15 characters we can assume length will
  // never exceed that limit.
  const auto& user_name = user_profile->name();

  // buffer_len includes null-terminator. user_name does not.
  const uint32_t bytes_to_copy = std::min(
      buffer_len.value(), static_cast<uint32_t>(user_name.length()) + 1);

  char* str_buffer = kernel_memory()->TranslateVirtual<char*>(buffer);
  xe::string_util::copy_truncating(str_buffer, user_name, bytes_to_copy);
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserGetName, kUserProfiles, kImplemented);

dword_result_t XamUserGetGamerTag_entry(dword_t user_index, dword_t buffer,
                                        dword_t buffer_len) {
  if (!buffer || buffer_len < 16) {
    return X_E_INVALIDARG;
  }

  if (user_index >= XUserMaxUserCount) {
    return X_E_INVALIDARG;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_ERROR_NO_SUCH_USER;
  }

  const auto& user_profile =
      kernel_state()->xam_state()->GetUserProfile(user_index);
  auto user_name = xe::to_utf16(user_profile->name());

  char16_t* str_buffer = kernel_memory()->TranslateVirtual<char16_t*>(buffer);

  xe::string_util::copy_and_swap_truncating(
      str_buffer, user_name, std::min(buffer_len.value(), uint32_t(16)));
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserGetGamerTag, kUserProfiles, kImplemented);

typedef struct {
  xe::be<uint32_t> setting_count;
  xe::be<uint32_t> settings_ptr;
} X_USER_READ_PROFILE_SETTINGS;
static_assert_size(X_USER_READ_PROFILE_SETTINGS, 8);

// https://github.com/oukiar/freestyledash/blob/master/Freestyle/Tools/Generic/xboxtools.cpp
uint32_t XamUserReadProfileSettingsEx(uint32_t title_id, uint32_t user_index,
                                      uint32_t xuid_count, be<uint64_t>* xuids,
                                      uint32_t setting_count,
                                      be<uint32_t>* setting_ids, uint32_t unk,
                                      be<uint32_t>* buffer_size_ptr,
                                      uint8_t* buffer,
                                      XAM_OVERLAPPED* overlapped) {
  if (!xuid_count) {
    assert_null(xuids);
  } else {
    assert_true(xuid_count == 1);
    assert_not_null(xuids);
    // TODO(gibbed): allow proper lookup of arbitrary XUIDs
    // TODO(gibbed): we assert here, but in case a title passes xuid_count > 1
    // until it's implemented for release builds...
    xuid_count = 1;

    const uint64_t xuid = xuids[0];

    assert_true(IsValidXUID(xuid));

    if (kernel_state()->xam_state()->IsUserSignedIn(xuid)) {
      const auto& user_profile =
          kernel_state()->xam_state()->GetUserProfileAny(xuid);

      if (IsOnlineXUID(xuid)) {
        assert_true(xuid == user_profile->GetOnlineXUID());
      } else {
        assert_true(xuid == user_profile->xuid());
      }
    }
  }
  assert_zero(unk);  // probably flags

  // must have at least 1 to 32 settings
  if (setting_count < 1 || setting_count > 32) {
    return X_ERROR_INVALID_PARAMETER;
  }

  // buffer size pointer must be valid
  if (!buffer_size_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  // if buffer size is non-zero, buffer pointer must be valid
  auto buffer_size = static_cast<uint32_t>(*buffer_size_ptr);
  if (buffer_size && !buffer) {
    return X_ERROR_INVALID_PARAMETER;
  }

  uint32_t needed_header_size = 0;
  uint32_t needed_data_size = 0;
  for (uint32_t i = 0; i < setting_count; ++i) {
    needed_header_size += sizeof(X_USER_PROFILE_SETTING);
    AttributeKey setting_key;
    setting_key.value = static_cast<uint32_t>(setting_ids[i]);
    switch (static_cast<X_USER_DATA_TYPE>(setting_key.type)) {
      case X_USER_DATA_TYPE::WSTRING:
      case X_USER_DATA_TYPE::BINARY:
        needed_data_size += setting_key.size;
        break;
      default:
        break;
    }
  }
  if (xuids) {
    needed_header_size *= xuid_count;
    needed_data_size *= xuid_count;
  }
  needed_header_size += sizeof(X_USER_READ_PROFILE_SETTINGS);

  uint32_t needed_size = needed_header_size + needed_data_size;
  if (!buffer || buffer_size < needed_size) {
    if (!buffer_size) {
      *buffer_size_ptr = needed_size;
    }
    return X_ERROR_INSUFFICIENT_BUFFER;
  }

  auto user_profile = kernel_state()->xam_state()->GetUserProfile(user_index);

  if (!user_profile && !xuids) {
    if (overlapped) {
      kernel_state()->CompleteOverlappedImmediate(
          kernel_state()->memory()->HostToGuestVirtual(overlapped),
          X_ERROR_NO_SUCH_USER);
      return X_ERROR_IO_PENDING;
    }
    return X_ERROR_NO_SUCH_USER;
  }

  if (xuids) {
    const uint64_t user_xuid = xuids[0];

    if (IsOnlineXUID(user_xuid)) {
      XELOGI("Looking up remote profile settings XUID: {:016X}", user_xuid);
    }

    if (!kernel_state()->xam_state()->IsUserSignedIn(user_xuid)) {
      if (overlapped) {
        kernel_state()->CompleteOverlappedImmediate(
            kernel_state()->memory()->HostToGuestVirtual(overlapped),
            X_ERROR_NO_SUCH_USER);
        return X_ERROR_IO_PENDING;
      }
      return X_ERROR_NO_SUCH_USER;
    }
    user_profile = kernel_state()->xam_state()->GetUserProfileAny(user_xuid);
  }

  if (!user_profile) {
    return X_ERROR_NO_SUCH_USER;
  }

  // First call asks for size (fill buffer_size_ptr).
  // Second call asks for buffer contents with that size.

  // TODO(gibbed): setting validity checking without needing a user profile
  // object.
  bool any_missing = false;
  for (uint32_t i = 0; i < setting_count; ++i) {
    auto setting_id = static_cast<uint32_t>(setting_ids[i]);
    auto setting = user_profile->GetSetting(setting_id);
    if (!setting) {
      any_missing = true;
      XELOGE(
          "xeXamUserReadProfileSettingsEx requested unimplemented setting "
          "{:08X}",
          setting_id);
    }
  }
  if (any_missing) {
    // TODO(benvanik): don't fail? most games don't even check!
    if (overlapped) {
      kernel_state()->CompleteOverlappedImmediate(
          kernel_state()->memory()->HostToGuestVirtual(overlapped),
          X_ERROR_INVALID_PARAMETER);
      return X_ERROR_IO_PENDING;
    }
    return X_ERROR_INVALID_PARAMETER;
  }

  auto out_header = reinterpret_cast<X_USER_READ_PROFILE_SETTINGS*>(buffer);
  auto out_setting = reinterpret_cast<X_USER_PROFILE_SETTING*>(&out_header[1]);
  out_header->setting_count = static_cast<uint32_t>(setting_count);
  out_header->settings_ptr =
      kernel_state()->memory()->HostToGuestVirtual(out_setting);

  DataByteStream out_stream(
      kernel_state()->memory()->HostToGuestVirtual(buffer), buffer, buffer_size,
      needed_header_size);
  for (uint32_t n = 0; n < setting_count; ++n) {
    uint32_t setting_id = setting_ids[n];
    auto setting = user_profile->GetSetting(setting_id);

    std::memset(out_setting, 0, sizeof(X_USER_PROFILE_SETTING));
    out_setting->from =
        !setting ? 0 : static_cast<uint32_t>(setting->GetSettingSource());
    if (xuids) {
      const uint64_t user_xuid = xuids[0];

      if (IsOnlineXUID(user_xuid)) {
        out_setting->xuid = user_profile->GetOnlineXUID();
      } else {
        out_setting->xuid = user_profile->xuid();
      }
    } else {
      out_setting->xuid = -1;
      out_setting->user_index = user_index;
    }
    out_setting->setting_id = setting_id;

    if (setting) {
      out_setting->data.type = static_cast<X_USER_DATA_TYPE>(
          setting->GetSettingHeader()->setting_type.value);
      setting->GetSettingData()->Append(&out_setting->data, &out_stream);
    }
    ++out_setting;
  }

  if (overlapped) {
    kernel_state()->CompleteOverlappedImmediate(
        kernel_state()->memory()->HostToGuestVirtual(overlapped),
        X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
  }
  return X_ERROR_SUCCESS;
}

dword_result_t XamUserReadProfileSettings_entry(
    dword_t title_id, dword_t user_index, dword_t xuid_count, lpqword_t xuids,
    dword_t setting_count, lpdword_t setting_ids, lpdword_t buffer_size_ptr,
    lpvoid_t buffer_ptr, pointer_t<XAM_OVERLAPPED> overlapped) {
  return XamUserReadProfileSettingsEx(title_id, user_index, xuid_count, xuids,
                                      setting_count, setting_ids, 0,
                                      buffer_size_ptr, buffer_ptr, overlapped);
}
DECLARE_XAM_EXPORT1(XamUserReadProfileSettings, kUserProfiles, kImplemented);

dword_result_t XamUserReadProfileSettingsEx_entry(
    dword_t title_id, dword_t user_index, dword_t xuid_count, lpqword_t xuids,
    dword_t setting_count, lpdword_t setting_ids, lpdword_t buffer_size_ptr,
    dword_t unk_2, lpvoid_t buffer_ptr, pointer_t<XAM_OVERLAPPED> overlapped) {
  return XamUserReadProfileSettingsEx(title_id, user_index, xuid_count, xuids,
                                      setting_count, setting_ids, unk_2,
                                      buffer_size_ptr, buffer_ptr, overlapped);
}
DECLARE_XAM_EXPORT1(XamUserReadProfileSettingsEx, kUserProfiles, kImplemented);

dword_result_t XamUserWriteProfileSettings_entry(
    dword_t title_id, dword_t user_index, dword_t setting_count,
    pointer_t<X_USER_PROFILE_SETTING> settings,
    pointer_t<XAM_OVERLAPPED> overlapped) {
  if (!setting_count || !settings) {
    return X_ERROR_INVALID_PARAMETER;
  }
  // Update and save settings.
  const auto& user_profile =
      kernel_state()->xam_state()->GetUserProfile(user_index);

  // Skip writing data about users with id != 0 they're not supported
  if (!user_profile) {
    if (overlapped) {
      kernel_state()->CompleteOverlappedImmediate(
          kernel_state()->memory()->HostToGuestVirtual(overlapped),
          X_ERROR_NO_SUCH_USER);
      return X_ERROR_IO_PENDING;
    }
    return X_ERROR_SUCCESS;
  }

  for (uint32_t n = 0; n < setting_count; ++n) {
    const X_USER_PROFILE_SETTING& setting = settings[n];

    auto setting_type = static_cast<X_USER_DATA_TYPE>(setting.data.type);
    if (setting_type == X_USER_DATA_TYPE::UNSET) {
      continue;
    }

    XELOGD(
        "XamUserWriteProfileSettings: setting index [{}]:"
        " from={} setting_id={:08X} data.type={}",
        n, (uint32_t)setting.from, (uint32_t)setting.setting_id,
        static_cast<uint32_t>(setting.data.type));

    switch (setting_type) {
      case X_USER_DATA_TYPE::CONTENT:
      case X_USER_DATA_TYPE::BINARY: {
        uint8_t* binary_ptr =
            kernel_state()->memory()->TranslateVirtual(setting.data.binary.ptr);

        size_t binary_size = setting.data.binary.size;
        std::vector<uint8_t> bytes;
        if (setting.data.binary.ptr) {
          // Copy provided data
          bytes.resize(binary_size);
          std::memcpy(bytes.data(), binary_ptr, binary_size);
        } else {
          // Data pointer was NULL, so just fill with zeroes
          bytes.resize(binary_size, 0);
        }

        auto user_setting =
            std::make_unique<UserSetting>(setting.setting_id, bytes);

        user_setting->SetNewSettingSource(X_USER_PROFILE_SETTING_SOURCE::TITLE);
        user_profile->AddSetting(std::move(user_setting));
      } break;
      case X_USER_DATA_TYPE::WSTRING:
      case X_USER_DATA_TYPE::DOUBLE:
      case X_USER_DATA_TYPE::FLOAT:
      case X_USER_DATA_TYPE::INT32:
      case X_USER_DATA_TYPE::INT64:
      case X_USER_DATA_TYPE::DATETIME:
      default: {
        XELOGE("XamUserWriteProfileSettings: Unimplemented data type {}",
               static_cast<uint32_t>(setting_type));
      } break;
    };
  }

  if (overlapped) {
    kernel_state()->CompleteOverlappedImmediate(overlapped, X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserWriteProfileSettings, kUserProfiles, kImplemented);

dword_result_t XamUserCheckPrivilege_entry(dword_t user_index, dword_t type,
                                           lpdword_t out_value) {
  // checking all users?
  if (user_index != XUserIndexAny) {
    if (user_index >= XUserMaxUserCount) {
      return X_ERROR_INVALID_PARAMETER;
    }

    if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
      return X_ERROR_NO_SUCH_USER;
    }
  }

  // If we deny everything, games should hopefully not try to do stuff.
  *out_value = 0;

  if (user_index == XUserIndexAny) {
    if (!kernel_state()
             ->xam_state()
             ->profile_manager()
             ->IsAnyProfileSignedIn()) {
      return X_ERROR_NOT_LOGGED_ON;
    }

    for (uint32_t i = 0; i < XUserMaxUserCount; i++) {
      if (kernel_state()->xam_state()->IsUserSignedIn(i)) {
        const auto& user_profile =
            kernel_state()->xam_state()->GetUserProfile(i);

        if (!user_profile->IsLiveEnabled()) {
          return X_ERROR_NOT_LOGGED_ON;
        }
      }
    }
  } else {
    if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
      return X_ERROR_NOT_LOGGED_ON;
    }

    const auto& user_profile =
        kernel_state()->xam_state()->GetUserProfile(user_index);

    if (!user_profile->IsLiveEnabled()) {
      return X_ERROR_NOT_LOGGED_ON;
    }
  }

  // Allow all privileges including multiplayer for live enabled profiles
  *out_value = 1;

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserCheckPrivilege, kUserProfiles, kStub);

dword_result_t XamUserContentRestrictionGetFlags_entry(dword_t user_index,
                                                       lpdword_t out_flags) {
  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_ERROR_NO_SUCH_USER;
  }

  // No restrictions?
  *out_flags = 0;
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserContentRestrictionGetFlags, kUserProfiles, kStub);

dword_result_t XamUserContentRestrictionGetRating_entry(dword_t user_index,
                                                        dword_t unk1,
                                                        lpdword_t out_unk2,
                                                        lpdword_t out_unk3) {
  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_ERROR_NO_SUCH_USER;
  }

  // Some games have special case paths for 3F that differ from the failure
  // path, so my guess is that's 'don't care'.
  *out_unk2 = 0x3F;
  *out_unk3 = 0;
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserContentRestrictionGetRating, kUserProfiles, kStub);

dword_result_t XamUserContentRestrictionCheckAccess_entry(
    dword_t user_index, dword_t unk1, dword_t unk2, dword_t unk3, dword_t unk4,
    lpdword_t out_unk5, dword_t overlapped_ptr) {
  *out_unk5 = 1;

  if (overlapped_ptr) {
    // TODO(benvanik): does this need the access arg on it?
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_SUCCESS);
  }

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserContentRestrictionCheckAccess, kUserProfiles, kStub);

dword_result_t XamUserIsOnlineEnabled_entry(dword_t user_index) {
  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_ERROR_NO_SUCH_USER;
  }

  const auto profile = kernel_state()->xam_state()->GetUserProfile(user_index);

  return profile->signin_state() == X_USER_SIGNIN_STATE::SignedInToLive;
}
DECLARE_XAM_EXPORT1(XamUserIsOnlineEnabled, kUserProfiles, kStub);

dword_result_t XamUserGetMembershipTier_entry(dword_t user_index) {
  if (user_index >= XUserMaxUserCount) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_ERROR_NO_SUCH_USER;
  }

  return X_XAMACCOUNTINFO::AccountSubscriptionTier::kSubscriptionTierGold;
}
DECLARE_XAM_EXPORT1(XamUserGetMembershipTier, kUserProfiles, kStub);

dword_result_t XamUserGetOnlineCountryFromXUID_entry(qword_t xuid) {
  return cvars::user_country;
}
DECLARE_XAM_EXPORT1(XamUserGetOnlineCountryFromXUID, kUserProfiles, kStub);

dword_result_t XamUserGetMembershipTierFromXUID_entry(qword_t xuid) {
  return X_XAMACCOUNTINFO::AccountSubscriptionTier::kSubscriptionTierGold;
}
DECLARE_XAM_EXPORT1(XamUserGetMembershipTierFromXUID, kUserProfiles, kStub);

dword_result_t XamUserAreUsersFriends_entry(
    dword_t user_index, pointer_t<xe::be<uint64_t>> xuids_ptr,
    dword_t xuids_count, lpdword_t out_value, dword_t overlapped_ptr) {
  bool are_friends = false;
  X_RESULT result;

  if (user_index >= XUserMaxUserCount) {
    result = X_ERROR_INVALID_PARAMETER;
  } else {
    if (kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
      const auto& user_profile =
          kernel_state()->xam_state()->GetUserProfile(user_index);
      if (!user_profile->IsLiveEnabled() ||
          user_profile->signin_state() == X_USER_SIGNIN_STATE::NotSignedIn) {
        result = X_ERROR_NOT_LOGGED_ON;
      } else {
        uint32_t friend_count = 0;

        for (uint32_t i = 0; i < xuids_count; i++) {
          const xe::be<uint64_t> xuid = xuids_ptr[i];

          const bool is_friend = user_profile->IsFriend(xuid);

          if (is_friend) {
            friend_count++;
          }
        }

        are_friends = friend_count == xuids_count;
        result = X_ERROR_SUCCESS;
      }
    } else {
      // Only support user 0.
      result =
          X_ERROR_NO_SUCH_USER;  // if user is local -> X_ERROR_NOT_LOGGED_ON
    }
  }

  if (out_value) {
    assert_true(!overlapped_ptr);
    *out_value = result == X_ERROR_SUCCESS ? are_friends : 0;
    return result;
  } else if (overlapped_ptr) {
    assert_true(!out_value);
    kernel_state()->CompleteOverlappedImmediateEx(
        overlapped_ptr,
        result == X_ERROR_SUCCESS ? X_ERROR_SUCCESS : X_ERROR_FUNCTION_FAILED,
        X_HRESULT_FROM_WIN32(result),
        result == X_ERROR_SUCCESS ? are_friends : 0);
    return X_ERROR_IO_PENDING;
  } else {
    assert_always();
    return X_ERROR_INVALID_PARAMETER;
  }
}
DECLARE_XAM_EXPORT1(XamUserAreUsersFriends, kUserProfiles, kImplemented);

dword_result_t XamUserGetAgeGroup_entry(
    dword_t user_index, lpdword_t age_ptr,
    pointer_t<XAM_OVERLAPPED> overlapped_ptr) {
  if (!age_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_ERROR_NO_SUCH_USER;
  }

  auto run = [user_index, age_ptr, overlapped_ptr](
                 uint32_t& extended_error, uint32_t& length) -> X_RESULT {
    X_RESULT result = X_ERROR_SUCCESS;

    *age_ptr = X_USER_AGE_GROUP::ADULT;

    extended_error = X_HRESULT_FROM_WIN32(result);
    length = 0;

    return result;
  };

  if (!overlapped_ptr) {
    uint32_t extended_error, length;
    return run(extended_error, length);
  } else {
    kernel_state()->CompleteOverlappedDeferredEx(run, overlapped_ptr);
    return X_ERROR_IO_PENDING;
  }
}
DECLARE_XAM_EXPORT1(XamUserGetAgeGroup, kUserProfiles, kImplemented);

dword_result_t XamUserCreateAchievementEnumerator_entry(
    dword_t title_id, dword_t user_index, qword_t xuid, dword_t flags,
    dword_t offset, dword_t count, lpdword_t buffer_size_ptr,
    lpdword_t handle_ptr) {
  if (!count || !buffer_size_ptr || !handle_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (user_index >= XUserMaxUserCount) {
    return X_ERROR_INVALID_PARAMETER;
  }

  size_t entry_size = sizeof(X_ACHIEVEMENT_DETAILS);
  if (flags & 7) {
    entry_size += X_ACHIEVEMENT_DETAILS::kStringBufferSize;
  }

  if (buffer_size_ptr) {
    *buffer_size_ptr = static_cast<uint32_t>(entry_size) * count;
  }

  auto e = object_ref<XAchievementEnumerator>(
      new XAchievementEnumerator(kernel_state(), count, flags));
  auto result = e->Initialize(user_index, 0xFB, 0xB000A, 0xB000B, 0);
  if (XFAILED(result)) {
    return result;
  }

  const auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    return X_ERROR_INVALID_PARAMETER;
  }

  uint64_t requester_xuid = user->xuid();
  if (xuid) {
    requester_xuid = xuid;
  }

  const util::XdbfGameData db = kernel_state()->title_xdbf();
  uint32_t title_id_ =
      title_id ? static_cast<uint32_t>(title_id) : kernel_state()->title_id();

  const auto user_title_achievements =
      kernel_state()->achievement_manager()->GetTitleAchievements(
          requester_xuid, title_id_);

  if (user_title_achievements) {
    for (const auto& entry : *user_title_achievements) {
      auto item = XAchievementEnumerator::AchievementDetails{
          entry.achievement_id,
          xe::load_and_swap<std::u16string>(entry.achievement_name.c_str()),
          xe::load_and_swap<std::u16string>(entry.unlocked_description.c_str()),
          xe::load_and_swap<std::u16string>(entry.locked_description.c_str()),
          entry.image_id,
          entry.gamerscore,
          entry.unlock_time.high_part,
          entry.unlock_time.low_part,
          entry.flags};

      e->AppendItem(item);
    }
  }

  *handle_ptr = e->handle();
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserCreateAchievementEnumerator, kUserProfiles,
                    kSketchy);

dword_result_t XamUserCreateTitlesPlayedEnumerator_entry(
    dword_t title_id, dword_t user_index, qword_t xuid, dword_t starting_index,
    dword_t game_count, lpdword_t buffer_size_ptr, lpdword_t handle_ptr) {
  if (user_index >= XUserMaxUserCount && game_count != 0 && !buffer_size_ptr &&
      !handle_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }
  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_ERROR_INVALID_PARAMETER;
  }

  // auto e = new XStaticEnumerator<X_XDBF_GPD_TITLEPLAYED>(kernel_state(),
  // game_count); auto result = e->Initialize(user_index, 0xFB, 0xB0050,
  // 0xB000B, 0x20, game_count, 0);

  XELOGD("XamUserCreateTitlesPlayedEnumerator: Stubbed");

  return X_ERROR_FUNCTION_FAILED;
}
DECLARE_XAM_EXPORT1(XamUserCreateTitlesPlayedEnumerator, kUserProfiles, kStub);

dword_result_t XamParseGamerTileKey_entry(lpdword_t key_ptr, lpdword_t out1_ptr,
                                          lpdword_t out2_ptr,
                                          lpdword_t out3_ptr) {
  *out1_ptr = 0xC0DE0001;
  *out2_ptr = 0xC0DE0002;
  *out3_ptr = 0xC0DE0003;
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamParseGamerTileKey, kUserProfiles, kStub);

dword_result_t XamReadTileToTexture_entry(dword_t unknown, dword_t title_id,
                                          qword_t tile_id, dword_t user_index,
                                          lpvoid_t buffer_ptr, dword_t stride,
                                          dword_t height,
                                          dword_t overlapped_ptr) {
  // TODO(gibbed): unknown=0,2,3,9
  if (!tile_id) {
    return X_ERROR_INVALID_PARAMETER;
  }

  size_t size = size_t(stride) * size_t(height);
  std::memset(buffer_ptr, 0xFF, size);

  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamReadTileToTexture, kUserProfiles, kStub);

dword_result_t XamWriteGamerTile_entry(dword_t arg1, dword_t arg2, dword_t arg3,
                                       dword_t arg4, dword_t arg5,
                                       dword_t overlapped_ptr) {
  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamWriteGamerTile, kUserProfiles, kStub);

dword_result_t XamSessionCreateHandle_entry(lpdword_t handle_ptr) {
  auto e = object_ref<XSession>(new XSession(kernel_state()));
  auto result = (uint32_t)e->Initialize();
  if (XFAILED(result)) {
    return result;
  }

  *handle_ptr = e->handle();
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamSessionCreateHandle, kUserProfiles, kImplemented);

dword_result_t XamSessionRefObjByHandle_entry(dword_t handle,
                                              lpdword_t obj_ptr) {
  auto object = kernel_state()->object_table()->LookupObject<XSession>(handle);
  if (!object) {
    return X_STATUS_INVALID_HANDLE;
  }

  object->RetainHandle();

  *obj_ptr = (uint32_t)object->guest_object();
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamSessionRefObjByHandle, kUserProfiles, kImplemented);

dword_result_t XamUserIsUnsafeProgrammingAllowed_entry(dword_t user_index,
                                                       dword_t unk,
                                                       lpdword_t result_ptr) {
  if (!result_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (user_index != XUserIndexAny && user_index >= XUserMaxUserCount) {
    return X_ERROR_INVALID_PARAMETER;
  }

  // uint32_t result = XamUserCheckPrivilege_entry(user_index, 0xD4u,
  // result_ptr);

  *result_ptr = 1;

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserIsUnsafeProgrammingAllowed, kUserProfiles, kStub);

dword_result_t XamUserGetSubscriptionType_entry(dword_t user_index,
                                                dword_t unk2, dword_t unk3) {
  if (user_index >= XUserMaxUserCount) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!unk2 || !unk3) {
    return X_E_INVALIDARG;
  }

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserGetSubscriptionType, kUserProfiles, kStub);

dword_result_t XamUserGetUserFlags_entry(dword_t user_index) {
  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return 0;
  }

  const auto& user_profile =
      kernel_state()->xam_state()->GetUserProfile(user_index);

  return user_profile->GetCachedFlags();
}
DECLARE_XAM_EXPORT1(XamUserGetUserFlags, kUserProfiles, kImplemented);

dword_result_t XamUserGetUserFlagsFromXUID_entry(qword_t xuid) {
  if (!kernel_state()->xam_state()->IsUserSignedIn(xuid)) {
    return 0;
  }

  const auto& user_profile =
      kernel_state()->xam_state()->GetUserProfileAny(xuid);

  return user_profile->GetCachedFlags();
}
DECLARE_XAM_EXPORT1(XamUserGetUserFlagsFromXUID, kUserProfiles, kImplemented);

dword_result_t XamUserGetOnlineLanguageFromXUID_entry(qword_t xuid) {
  /* Notes:
     - Calls XamUserGetUserFlagsFromXUID and returns (ulonglong)(cached_flag <<
     0x20) >> 0x39 & 0x1f;
     - XamUserGetMembershipTierFromXUID and XamUserGetOnlineCountryFromXUID also
     call it
     - Removed in metro
  */
  return cvars::user_language;
}
DECLARE_XAM_EXPORT1(XamUserGetOnlineLanguageFromXUID, kUserProfiles, kStub);

constexpr uint8_t kStatsMaxAmount = 64;

struct X_STATS_DETAILS {
  xe::be<uint32_t> id;
  xe::be<uint32_t> stats_amount;
  xe::be<uint16_t> stats[kStatsMaxAmount];
};
static_assert_size(X_STATS_DETAILS, 8 + kStatsMaxAmount * 2);

dword_result_t XamUserCreateStatsEnumerator_entry(
    dword_t title_id, dword_t enumerator_type, qword_t pivot_user,
    dword_t num_rows, dword_t num_stats_specs,
    pointer_t<X_USER_STATS_SPEC> stats_ptr, lpdword_t buffer_size_ptr,
    lpdword_t handle_ptr) {
  assert_false(enumerator_type > X_STATS_ENUMERATOR_TYPE::BY_RATING);

  if (!pivot_user || !stats_ptr || !buffer_size_ptr || !handle_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!num_rows || num_rows > kXUserMaxStatsRows) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!num_stats_specs) {
    return X_ERROR_INVALID_PARAMETER;
  }

  // auto e = object_ref<XUserStatsEnumerator>(new XUserStatsEnumerator(
  //     kernel_state(), stats_ptr->num_column_ids, enumerator_type));

  auto e = make_object<XStaticEnumerator<X_USER_STATS_SPEC>>(
      kernel_state(), stats_ptr->num_column_ids);

  const X_STATUS result =
      e->Initialize(XUserIndexNone, 0xFB, 0xB0023, 0xB0024, 0);

  if (XFAILED(result)) {
    return result;
  }

  const X_STATS_ENUMERATOR_TYPE type =
      static_cast<X_STATS_ENUMERATOR_TYPE>(enumerator_type.value());

  // pivot
  uint64_t xuid = 0;

  switch (type) {
    case X_STATS_ENUMERATOR_TYPE::XUID: {
      xuid = pivot_user;
      XELOGI("XamUserCreateStatsEnumeratorByXuid: {:016X}", xuid);
    } break;
    case X_STATS_ENUMERATOR_TYPE::RANK: {
      xuid = pivot_user & 0xFFFF;
      XELOGI("XamUserCreateStatsEnumeratorByRank: {:08X}", xuid);
    } break;
    case X_STATS_ENUMERATOR_TYPE::RANK_PER_SPEC: {
      xuid = pivot_user;
      XELOGI("XamUserCreateStatsEnumeratorByRankPreSpec: {:016X}", xuid);
    } break;
    case X_STATS_ENUMERATOR_TYPE::BY_RATING: {
      xuid = pivot_user;
      XELOGI("XamUserCreateStatsEnumeratorByRating: {:016X}", xuid);
    } break;
    default:
      break;
  }

  // start = Rank, Raiting, XUID?
  for (auto i = 0; i < e->items_per_enumerate(); i++) {
    //  XUserStatsEnumerator::StatsSpec stat = {};
    //  stat.NumColumnIds = 0;
    //  stat.ViewId = stats_ptr->view_id;
    //  e->AppendItem(stat);

    X_USER_STATS_SPEC* item = e->AppendItem();
  }

  // XUSER_STATS_READ_RESULTS?
  if (buffer_size_ptr) {
    *buffer_size_ptr = stats_ptr->num_column_ids * sizeof(X_USER_STATS_SPEC);
  }

  *handle_ptr = e->handle();
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserCreateStatsEnumerator, kUserProfiles, kSketchy);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(User);