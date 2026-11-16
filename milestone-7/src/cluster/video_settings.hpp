#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

namespace aa_cluster {
// Generic names take precedence; legacy names keep existing installations and
// private benches working through the source-layout migration.
inline const char* video_environment(const char* name, const char* legacy) {
    const char* value = getenv(name);
    return value ? value : getenv(legacy);
}

static const char* const ActiveMarker = "/dev/shmem/nbt-aad-kombi-video.active";
// A two-byte preference, never a video buffer. An explicit 0 also overrides
// the legacy trial's NBT_G30_CLUSTER_VIDEO=1 startup default.
inline const char* setting_path() {
    const char* override_value =
        video_environment("NBT_CLUSTER_SETTING", "NBT_G30_CLUSTER_SETTING");
    return override_value && *override_value ? override_value : "/var/opt/aa/kombi-video";
}
inline bool setting_enabled() {
    FILE* preference = fopen(setting_path(), "rb");
    if (preference) {
        int first_byte = fgetc(preference);
        fclose(preference);
        return first_byte == '1';
    }
    const char* override_value = video_environment("NBT_CLUSTER_VIDEO", "NBT_G30_CLUSTER_VIDEO");
    return override_value && !strcmp(override_value, "1");
}
inline bool setting_write(bool enabled) {
    const char* path = setting_path();
    if (!video_environment("NBT_CLUSTER_SETTING", "NBT_G30_CLUSTER_SETTING")) {
        mkdir("/var/opt/aa", 0755);
    }
    // Publish by rename so a reader sees either the old or new preference,
    // never a partially written file. The temporary name is exclusive per PID.
    char temporary_path[512];
    int path_length =
        snprintf(temporary_path, sizeof(temporary_path), "%s.new.%ld", path, (long)getpid());
    if (path_length <= 0 || unsigned(path_length) >= sizeof(temporary_path)) {
        return false;
    }
    int temporary_fd = open(temporary_path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (temporary_fd < 0) {
        return false;
    }
    bool saved = write(temporary_fd, enabled ? "1\n" : "0\n", 2) == 2;
    if (close(temporary_fd)) {
        saved = false;
    }
    if (saved) {
        saved = rename(temporary_path, path) == 0;
    }
    if (!saved) {
        unlink(temporary_path);
    }
    return saved;
}
} // namespace aa_cluster
