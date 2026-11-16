#include "cluster/video_settings.hpp"
#include <assert.h>
#include <stdio.h>
int main() {
    char temporary_directory[] = "/tmp/aa-map-setting-XXXXXX";
    assert(mkdtemp(temporary_directory));
    char setting_path[256];
    snprintf(setting_path, sizeof(setting_path), "%s/setting", temporary_directory);
    setenv("NBT_G30_CLUSTER_SETTING", setting_path, 1);
    unsetenv("NBT_G30_CLUSTER_VIDEO");
    unsetenv("NBT_CLUSTER_SETTING");
    unsetenv("NBT_CLUSTER_VIDEO");
    // With neither a persisted setting nor an environment override, default off.
    assert(!aa_cluster::setting_enabled());
    assert(aa_cluster::setting_write(true));
    assert(aa_cluster::setting_enabled());
    assert(aa_cluster::setting_write(false));

    // An explicit persisted off value wins over the legacy environment flag.
    setenv("NBT_G30_CLUSTER_VIDEO", "1", 1);
    assert(!aa_cluster::setting_enabled());
    unlink(setting_path);
    assert(aa_cluster::setting_enabled());

    // New generic overrides win over legacy names, including an explicit off.
    setenv("NBT_CLUSTER_VIDEO", "0", 1);
    assert(!aa_cluster::setting_enabled());
    setenv("NBT_CLUSTER_VIDEO", "1", 1);
    assert(aa_cluster::setting_enabled());
    setenv("NBT_G30_CLUSTER_SETTING", "/unavailable/legacy-setting", 1);
    setenv("NBT_CLUSTER_SETTING", setting_path, 1);
    assert(aa_cluster::setting_write(false));
    assert(!aa_cluster::setting_enabled());

    // An invalid existing file fails closed; a missing directory fails writes.
    FILE *setting_file = fopen(setting_path, "wb");
    assert(setting_file);
    fputs("invalid", setting_file);
    fclose(setting_file);
    assert(!aa_cluster::setting_enabled());
    unlink(setting_path);
    rmdir(temporary_directory);
    assert(!aa_cluster::setting_write(true));
    puts("AA_LIVE_SETTING_OK default off, persisted on/off, explicit off wins, malformed/write "
         "failure");
}
