/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/common/importer/chrome_importer_utils.h"

#include <memory>
#include <optional>
#include <utility>

#include "base/check.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/values.h"
#include "brave/common/importer/importer_constants.h"
#include "brave/common/importer/scoped_copy_file.h"
#include "components/user_data_importer/common/importer_data_types.h"
#include "components/webdata/common/webdata_constants.h"
#include "sql/database.h"
#include "sql/statement.h"

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "extensions/common/extension.h"
#include "extensions/common/manifest.h"
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS)
using extensions::Extension;
using extensions::Manifest;
#endif

namespace {
#if BUILDFLAG(ENABLE_EXTENSIONS)

std::optional<base::Value::Dict> GetChromeExtensionsListFromFile(
    const base::FilePath& preference_path) {
  if (!base::PathExists(preference_path))
    return std::nullopt;

  std::string preference_content;
  base::ReadFileToString(preference_path, &preference_content);

  std::optional<base::Value::Dict> preference =
      base::JSONReader::ReadDict(preference_content);
  if (!preference) {
    return std::nullopt;
  }
  if (auto* extensions =
          preference->FindDictByDottedPath(kChromeExtensionsListPath)) {
    return std::move(*extensions);
  }
  return std::nullopt;
}

bool HasImportableExtensions(const base::FilePath& profile_path) {
  return GetImportableChromeExtensionsList(profile_path).has_value();
}

std::vector<std::string> GetImportableListFromChromeExtensionsList(
    const base::Value::Dict& extensions_list) {
  std::vector<std::string> extensions;
  for (const auto [key, value] : extensions_list) {
    if (!value.is_dict()) {
      continue;
    }
    const base::Value::Dict& dict = value.GetDict();
    // Only import if type is extension, it's came from webstore and it's not
    // installed by default.
    if (dict.FindBool("was_installed_by_default").value_or(true))
      continue;

    const auto state = dict.FindInt("state");
    if (state.has_value()) {
      // If `state` exists, probably it is an old browser version.
      if (state == 0) {
        // explicit `"state": 0` means disabled state
        continue;
      }
    } else if (const auto* disable_reasons = dict.FindList("disable_reasons");
               disable_reasons && !disable_reasons->empty()) {
      // For new browsers an extension is enabled if there is no disable reason.
      continue;
    }

    if (!dict.FindBool("from_webstore").value_or(false)) {
      continue;
    }

    if (auto* manifest_dict = dict.FindDict("manifest")) {
      if (Manifest::GetTypeFromManifestValue(*manifest_dict) ==
          Manifest::TYPE_EXTENSION) {
        extensions.push_back(key);
      }
    }
  }

  return extensions;
}

std::optional<base::Value::Dict> GetChromeExtensionsList(
    const base::FilePath& profile_path) {
  auto list_from_secure_preference = GetChromeExtensionsListFromFile(
      profile_path.AppendASCII(kChromeSecurePreferencesFile));

  auto list_from_preferences = GetChromeExtensionsListFromFile(
      profile_path.AppendASCII(kChromePreferencesFile));
  if (!list_from_secure_preference.has_value())
    return list_from_preferences;

  if (list_from_secure_preference.has_value() &&
      list_from_preferences.has_value()) {
    list_from_secure_preference->Merge(
        std::move(list_from_preferences.value()));
    return list_from_secure_preference;
  }

  return list_from_secure_preference;
}
#endif

bool HasPaymentMethods(const base::FilePath& payments_path) {
  if (!base::PathExists(payments_path))
    return false;

  ScopedCopyFile copy_payments_file(payments_path);
  if (!copy_payments_file.copy_success())
    return false;

  sql::Database db(sql::Database::Tag("Payments"));
  if (!db.Open(copy_payments_file.copied_file_path())) {
    return false;
  }

  constexpr char query[] = "SELECT name_on_card FROM credit_cards;";
  sql::Statement s(db.GetUniqueStatement(query));
  // Will return false if there is no payment info.
  return s.Step();
}

bool IsLastActiveProfile(const std::string& profile,
                         const base::Value::List& last_active_profiles) {
  for (const auto& it : last_active_profiles) {
    if (it.GetString() == profile) {
      return true;
    }
  }
  return false;
}

bool CanImportPasswordsForType(user_data_importer::ImporterType type) {
  // We can't import passwords from Chrome due to encryption. See
  // https://github.com/brave/brave-browser/issues/34046
  // #issuecomment-2857856039
  if (type == user_data_importer::TYPE_CHROME) {
    return false;
  }

  // We can import password from Whale only on macOS. Decryption fails on
  // Windows and Linux.
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
  if (type == user_data_importer::TYPE_WHALE) {
    return false;
  }
#endif

  return true;
}

}  // namespace

base::Value::List GetChromeSourceProfiles(
    const base::FilePath& local_state_path) {
  base::Value::List profiles;
  if (base::PathExists(local_state_path)) {
    std::string local_state_content;
    base::ReadFileToString(local_state_path, &local_state_content);
    std::optional<base::Value::Dict> local_state_dict =
        base::JSONReader::ReadDict(local_state_content);
    if (!local_state_dict)
      return profiles;

    const auto* profile_dict = local_state_dict->FindDict("profile");
    if (profile_dict) {
      const auto* last_active_profiles =
          profile_dict->FindList("last_active_profiles");

      const auto* info_cache = profile_dict->FindDict("info_cache");
      if (info_cache) {
        for (const auto value : *info_cache) {
          const auto* profile = value.second.GetIfDict();
          if (!profile)
            continue;

          auto* name = profile->FindString("name");
          if (!name) {
            continue;
          }
          base::Value::Dict entry;
          entry.Set("id", value.first);
          entry.Set("name", *name);
          if (last_active_profiles)
            entry.Set("last_active",
                      IsLastActiveProfile(value.first, *last_active_profiles));

          auto* avatar_icon = profile->FindString("avatar_icon");
          if (avatar_icon) {
            entry.Set("avatar_icon", *avatar_icon);
          }
          auto active_time = profile->FindDouble("active_time");
          if (active_time) {
            entry.Set("active_time", *active_time);
          }
          profiles.Append(std::move(entry));
        }
      }
    }
  }
  if (profiles.empty()) {
    base::Value::Dict entry;
    entry.Set("id", "");
    entry.Set("name", "Default");
    profiles.Append(std::move(entry));
  }
  return profiles;
}

bool ChromeImporterCanImport(const base::FilePath& profile,
                             user_data_importer::ImporterType type,
                             uint16_t* services_supported) {
  DCHECK(services_supported);
  *services_supported = user_data_importer::NONE;

  base::FilePath bookmarks = profile.Append(
      base::FilePath::StringType(FILE_PATH_LITERAL("Bookmarks")));
  if (base::PathExists(bookmarks)) {
    *services_supported |= user_data_importer::FAVORITES;
  }

  base::FilePath history =
      profile.Append(base::FilePath::StringType(FILE_PATH_LITERAL("History")));
  if (base::PathExists(history)) {
    *services_supported |= user_data_importer::HISTORY;
  }

  if (CanImportPasswordsForType(type)) {
    base::FilePath passwords = profile.Append(
        base::FilePath::StringType(FILE_PATH_LITERAL("Login Data")));
    base::FilePath passwords_for_account =
        profile.Append(base::FilePath::StringType(
            FILE_PATH_LITERAL("Login Data For Account")));
    if (base::PathExists(passwords) ||
        base::PathExists(passwords_for_account)) {
      *services_supported |= user_data_importer::PASSWORDS;
    }
  }

  if (HasPaymentMethods(profile.Append(kWebDataFilename)))
    *services_supported |= user_data_importer::PAYMENTS;

#if BUILDFLAG(ENABLE_EXTENSIONS)
  if (HasImportableExtensions(profile))
    *services_supported |= user_data_importer::EXTENSIONS;
#endif

  return *services_supported != user_data_importer::NONE;
}

#if BUILDFLAG(ENABLE_EXTENSIONS)
std::optional<std::vector<std::string>> GetImportableChromeExtensionsList(
    const base::FilePath& profile_path) {
  if (auto extensions = GetChromeExtensionsList(profile_path)) {
    return GetImportableListFromChromeExtensionsList(extensions.value());
  }
  return std::nullopt;
}
#endif
