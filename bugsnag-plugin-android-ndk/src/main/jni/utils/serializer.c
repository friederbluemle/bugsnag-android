#include "serializer.h"

#include <fcntl.h>
#include <parson/parson.h>
#include <report.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <utils/migrate.h>

#ifdef __cplusplus
extern "C" {
#endif
bool bsg_report_write(bsg_report_header *header, bugsnag_report *report,
                      int fd);

bugsnag_report *bsg_report_read(int fd);
bsg_report_header *bsg_report_header_read(int fd);

#ifdef __cplusplus
}
#endif

bool bsg_serialize_report_to_file(bsg_environment *env) {
  int fd = open(env->next_report_path, O_WRONLY | O_CREAT, 0644);
  if (fd == -1) {
    return false;
  }

  return bsg_report_write(&env->report_header, &env->next_report, fd);
}

bugsnag_report *bsg_deserialize_report_from_file(char *filepath) {
  int fd = open(filepath, O_RDONLY);
  if (fd == -1) {
    return NULL;
  }

  return bsg_report_read(fd);
}

bugsnag_report_v1 *bsg_report_v1_read(int fd) {
    size_t report_size = sizeof(bugsnag_report_v1);
    bugsnag_report_v1 *report = malloc(report_size);

    ssize_t len = read(fd, report, report_size);
    if (len != report_size) {
        free(report);
        return NULL;
    }
    return report;
}

bugsnag_report *bsg_report_v2_read(int fd) {
    size_t report_size = sizeof(bugsnag_report);
    bugsnag_report *report = malloc(report_size);

    ssize_t len = read(fd, report, report_size);
    if (len != report_size) {
      free(report);
      return NULL;
    }
    return report;
}

bugsnag_report *bsg_report_read(int fd) {
  bsg_report_header *header = bsg_report_header_read(fd);
  if (header == NULL) {
    return NULL;
  }

  int report_version = header->version;
  free(header);

  bugsnag_report *report = NULL;

  if (report_version == 1) { // 'report->unhandled_events' was added in v2
      bugsnag_report_v1 *report_v1 = bsg_report_v1_read(fd);

      if (report_v1 != NULL) {
          report = malloc(sizeof(bugsnag_report));

          report->notifier = report_v1->notifier;
          report->app = report_v1->app;
          report->device = report_v1->device;
          report->user = report_v1->user;
          report->exception = report_v1->exception;
          report->metadata = report_v1->metadata;
          report->crumb_count = report_v1->crumb_count;
          report->crumb_first_index = report_v1->crumb_first_index;

          size_t breadcrumb_size = sizeof(bugsnag_breadcrumb) * BUGSNAG_CRUMBS_MAX;
          memcpy(&report->breadcrumbs, report_v1->breadcrumbs, breadcrumb_size);

          strcpy(report->context, report_v1->context);
          report->severity = report_v1->severity;
          strcpy(report->session_id, report_v1->session_id);
          strcpy(report->session_start, report_v1->session_start);
          report->handled_events = report_v1->handled_events;
          report->unhandled_events = 1;

          free(report_v1);
      }
  } else {
      report = bsg_report_v2_read(fd);
  }
  return report;
}

bsg_report_header *bsg_report_header_read(int fd) {
  bsg_report_header *header = malloc(sizeof(bsg_report_header));
  ssize_t len = read(fd, header, sizeof(bsg_report_header));
  if (len != sizeof(bsg_report_header)) {
    free(header);
    return NULL;
  }

  return header;
}

bool bsg_report_header_write(bsg_report_header *header, int fd) {
  ssize_t len = write(fd, header, sizeof(bsg_report_header));

  return len == sizeof(bsg_report_header);
}

bool bsg_report_write(bsg_report_header *header, bugsnag_report *report,
                      int fd) {
  if (!bsg_report_header_write(header, fd)) {
    return false;
  }

  ssize_t len = write(fd, report, sizeof(bugsnag_report));
  return len == sizeof(bugsnag_report);
}

const char *bsg_crumb_type_string(bsg_breadcrumb_t type) {
  switch (type) {
  case BSG_CRUMB_ERROR:
    return "error";
  case BSG_CRUMB_LOG:
    return "log";
  case BSG_CRUMB_MANUAL:
    return "manual";
  case BSG_CRUMB_NAVIGATION:
    return "navigation";
  case BSG_CRUMB_PROCESS:
    return "process";
  case BSG_CRUMB_REQUEST:
    return "request";
  case BSG_CRUMB_STATE:
    return "state";
  case BSG_CRUMB_USER:
    return "user";
  }
}

const char *bsg_severity_string(bsg_severity_t type) {
  switch (type) {
  case BSG_SEVERITY_INFO:
    return "info";
  case BSG_SEVERITY_WARN:
    return "warn";
  case BSG_SEVERITY_ERR:
    return "error";
  }
}

void bsg_serialize_context(const bugsnag_report *report, JSON_Object *event) {
  if (strlen(report->context) > 0) {
    json_object_set_string(event, "context", report->context);
  } else {
    json_object_set_string(event, "context", report->app.active_screen);
  }
}

void bsg_serialize_handled_state(const bugsnag_report *report, JSON_Object *event) {
  // FUTURE(dm): severityReason/unhandled attributes are currently
  // over-optimized for signal handling. in the future we may want to handle
  // C++ exceptions, etc as well.
  json_object_set_string(event, "severity", bsg_severity_string(report->severity));
  json_object_dotset_boolean(event, "unhandled", true);
  json_object_dotset_string(event, "severityReason.type", "signal");
  json_object_dotset_string(event, "severityReason.attributes.signalType", report->exception.name);
}

void bsg_serialize_app(const bsg_app_info app, JSON_Object *event) {
  json_object_dotset_string(event, "app.version", app.version);
  json_object_dotset_string(event, "app.id", app.id);
  json_object_dotset_string(event, "app.type", app.type);

  json_object_dotset_string(event, "app.releaseStage", app.release_stage);
  json_object_dotset_number(event, "app.versionCode", app.version_code);
  if (strlen(app.build_uuid) > 0) {
    json_object_dotset_string(event, "app.buildUUID", app.build_uuid);
  }
  json_object_dotset_string(event, "app.binaryArch", app.binaryArch);
  json_object_dotset_number(event, "app.duration", app.duration);
  json_object_dotset_number(event, "app.durationInForeground", app.duration_in_foreground);
  json_object_dotset_boolean(event, "app.inForeground", app.in_foreground);
}

void bsg_serialize_app_metadata(const bsg_app_info app, JSON_Object *event) {
  json_object_dotset_string(event, "metaData.app.packageName", app.package_name);
  json_object_dotset_string(event, "metaData.app.versionName", app.version_name);
  json_object_dotset_string(event, "metaData.app.activeScreen", app.active_screen);
  json_object_dotset_string(event, "metaData.app.name", app.name);
  json_object_dotset_boolean(event, "metaData.app.lowMemory", app.low_memory);
}

void bsg_serialize_device(const bsg_device_info device, JSON_Object *event) {
  json_object_dotset_string(event, "device.osName", "android");
  json_object_dotset_string(event, "device.id", device.id);
  json_object_dotset_string(event, "device.osVersion", device.os_version);
  json_object_dotset_string(event, "device.manufacturer", device.manufacturer);
  json_object_dotset_string(event, "device.model", device.model);
  json_object_dotset_string(event, "device.orientation", device.orientation);
  json_object_dotset_number(event, "device.runtimeVersions.androidApiLevel", device.api_level);
  json_object_dotset_string(event, "device.runtimeVersions.osBuild", device.os_build);

  JSON_Value *abi_val = json_value_init_array();
  JSON_Array *cpu_abis = json_value_get_array(abi_val);
  json_object_dotset_value(event, "device.cpuAbi", abi_val);
  for (int i = 0; i < device.cpu_abi_count; i++) {
    json_array_append_string(cpu_abis, device.cpu_abi[i].value);
  }

  json_object_dotset_number(event, "device.totalMemory", device.total_memory);
}

void bsg_serialize_device_metadata(const bsg_device_info device, JSON_Object *event) {
  json_object_dotset_string(event, "metaData.device.brand", device.brand);
  json_object_dotset_boolean(event, "metaData.device.emulator", device.emulator);
  json_object_dotset_boolean(event, "metaData.device.jailbroken", device.jailbroken);
  json_object_dotset_string(event, "metaData.device.locale", device.locale);
  json_object_dotset_string(event, "metaData.device.locationStatus", device.location_status);
  json_object_dotset_string(event, "metaData.device.networkAccess", device.network_access);
  json_object_dotset_number(event, "metaData.device.dpi", device.dpi);
  json_object_dotset_number(event, "metaData.device.screenDensity", device.screen_density);
  json_object_dotset_string(event, "metaData.device.screenResolution", device.screen_resolution);

  char report_time[sizeof "2018-10-08T12:07:09Z"];
  if (device.time > 0) {
    strftime(report_time, sizeof report_time, "%FT%TZ", gmtime(&device.time));
    json_object_dotset_string(event, "metaData.device.time", report_time);
  }
}

void bsg_serialize_custom_metadata(const bugsnag_metadata metadata, JSON_Object *event) {
  for (int i = 0; i < metadata.value_count; i++) {
    char *format = malloc(sizeof(char) * 256);
    bsg_metadata_value value = metadata.values[i];

    switch (value.type) {
      case BSG_BOOL_VALUE:
        sprintf(format, "metaData.%s.%s", value.section, value.name);
            json_object_dotset_boolean(event, format, value.bool_value);
            break;
      case BSG_CHAR_VALUE:
        sprintf(format, "metaData.%s.%s", value.section, value.name);
            json_object_dotset_string(event, format, value.char_value);
            break;
      case BSG_NUMBER_VALUE:
        sprintf(format, "metaData.%s.%s", value.section, value.name);
            json_object_dotset_number(event, format, value.double_value);
            break;
      default:
        break;
    }
    free(format);
  }
}

void bsg_serialize_user(const bsg_user user, JSON_Object *event) {
  if (strlen(user.name) > 0)
    json_object_dotset_string(event, "user.name", user.name);
  if (strlen(user.email) > 0)
    json_object_dotset_string(event, "user.email", user.email);
  if (strlen(user.id) > 0)
    json_object_dotset_string(event, "user.id", user.id);
}

void bsg_serialize_session(bugsnag_report *report, JSON_Object *event) {
  if (bugsnag_report_has_session(report)) {
    json_object_dotset_string(event, "session.startedAt",
                              report->session_start);
    json_object_dotset_string(event, "session.id", report->session_id);
    json_object_dotset_number(event, "session.events.handled",
                              report->handled_events);
    json_object_dotset_number(event, "session.events.unhandled", report->unhandled_events);
  }
}

void bsg_serialize_exception(bsg_exception exc, JSON_Object *exception, JSON_Array *stacktrace) {
  json_object_set_string(exception, "errorClass", exc.name);
  json_object_set_string(exception, "message", exc.message);
  json_object_set_string(exception, "type", "c");
  for (int findex = 0; findex < exc.frame_count; findex++) {
    bsg_stackframe stackframe = exc.stacktrace[findex];
    bsg_serialize_stackframe(&stackframe, stacktrace);
  }
}

void bsg_serialize_stackframe(bsg_stackframe *stackframe, JSON_Array *stacktrace) {
  JSON_Value *frame_val = json_value_init_object();
  JSON_Object *frame = json_value_get_object(frame_val);
  json_object_set_number(frame, "frameAddress", (*stackframe).frame_address);
  json_object_set_number(frame, "symbolAddress", (*stackframe).symbol_address);
  json_object_set_number(frame, "loadAddress", (*stackframe).load_address);
  json_object_set_number(frame, "lineNumber", (*stackframe).line_number);
  if (strlen((*stackframe).filename) > 0) {
    json_object_set_string(frame, "file", (*stackframe).filename);
  }
  if (strlen((*stackframe).method) == 0) {
    char *frame_address = malloc(sizeof(char) * 32);
    sprintf(frame_address, "0x%lx",
            (unsigned long) (*stackframe).frame_address);
    json_object_set_string(frame, "method", frame_address);
    free(frame_address);
  } else {
    json_object_set_string(frame, "method", (*stackframe).method);
  }

  json_array_append_value(stacktrace, frame_val);
}

void bsg_serialize_breadcrumbs(const bugsnag_report *report, JSON_Array *crumbs) {
  if (report->crumb_count > 0) {
    int current_index = report->crumb_first_index;
    while (json_array_get_count(crumbs) < report->crumb_count) {
      JSON_Value *crumb_val = json_value_init_object();
      JSON_Object *crumb = json_value_get_object(crumb_val);
      json_array_append_value(crumbs, crumb_val);
      bugsnag_breadcrumb breadcrumb = report->breadcrumbs[current_index];
      json_object_set_string(crumb, "name", breadcrumb.name);
      json_object_set_string(crumb, "timestamp", breadcrumb.timestamp);
      json_object_set_string(crumb, "type",
                             bsg_crumb_type_string(breadcrumb.type));

      JSON_Value *meta_val = json_value_init_object();
      JSON_Object *meta = json_value_get_object(meta_val);
      json_object_set_value(crumb, "metaData", meta_val);
      int metadata_index = 0;
      while (strlen(breadcrumb.metadata[metadata_index].key) > 0) {
        json_object_set_string(meta, breadcrumb.metadata[metadata_index].key,
                               breadcrumb.metadata[metadata_index].value);
        metadata_index++;
      }

      current_index++;
      if (current_index == BUGSNAG_CRUMBS_MAX) {
        current_index = 0;
      }
    }
  }
}

char *bsg_serialize_report_to_json_string(bugsnag_report *report) {
  JSON_Value *event_val = json_value_init_object();
  JSON_Object *event = json_value_get_object(event_val);
  JSON_Value *crumbs_val = json_value_init_array();
  JSON_Array *crumbs = json_value_get_array(crumbs_val);
  JSON_Value *exceptions_val = json_value_init_array();
  JSON_Array *exceptions = json_value_get_array(exceptions_val);
  JSON_Value *ex_val = json_value_init_object();
  JSON_Object *exception = json_value_get_object(ex_val);
  JSON_Value *stack_val = json_value_init_array();
  JSON_Array *stacktrace = json_value_get_array(stack_val);
  json_object_set_value(event, "exceptions", exceptions_val);
  json_object_set_value(event, "breadcrumbs", crumbs_val);
  json_object_set_value(exception, "stacktrace", stack_val);
  json_array_append_value(exceptions, ex_val);
  char *serialized_string = NULL;
  {
    bsg_serialize_context(report, event);
    bsg_serialize_handled_state(report, event);
    bsg_serialize_app(report->app, event);
    bsg_serialize_app_metadata(report->app, event);
    bsg_serialize_device(report->device, event);
    bsg_serialize_device_metadata(report->device, event);
    bsg_serialize_custom_metadata(report->metadata, event);
    bsg_serialize_user(report->user, event);
    bsg_serialize_session(report, event);
    bsg_serialize_exception(report->exception, exception, stacktrace);
    bsg_serialize_breadcrumbs(report, crumbs);

    serialized_string = json_serialize_to_string(event_val);
    json_value_free(event_val);
  }
  return serialized_string;
}
