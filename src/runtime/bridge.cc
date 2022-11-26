#include <socket/platform.hh>
#include <socket/runtime.hh>
#include <socket/log.hh>

#define RESULT_CALLBACK(message, reply)                                        \
  [=](auto seq, auto json, auto data) {                                        \
    reply(Result { seq, message, json, data });                                \
  }

#define getMessageParam(var, name, parse, ...)                                 \
  try {                                                                        \
    var = parse(message.get(name, ##__VA_ARGS__));                             \
  } catch (...) {                                                              \
    return reply(Result::Err { message, JSON::Object::Entries {                \
      {"message", "Invalid '" name "' given in parameters"}                    \
    }});                                                                       \
  }

#if defined(__APPLE__)
  static dispatch_queue_attr_t qos = dispatch_queue_attr_make_with_qos_class(
    DISPATCH_QUEUE_CONCURRENT,
    QOS_CLASS_USER_INITIATED,
    -1
  );

  static dispatch_queue_t queue = dispatch_queue_create(
    "co.socketsupply.queue.bridge",
    qos
  );
#endif

namespace ssc::runtime::ipc {
  // forward

  JSON::Any validateMessageParameters (
    const Message& message,
    const Vector<String> names
  ) {
    for (const auto& name : names) {
      if (!message.has(name) || message.get(name).size() == 0) {
        return JSON::Object::Entries {
          {"message", "Expecting '" + name + "' in parameters"}
        };
      }
    }

    return nullptr;
  }

  void Bridge::init () {
    /**
      * Starts a bluetooth service
      * @param serviceId
      */
    router.map("bluetooth.start", [&](auto message, auto _, auto reply) {
      auto err = validateMessageParameters(message, {"serviceId"});

      if (err.type != JSON::Type::Null) {
        return reply(Result::Err { message, err });
      }

      bluetooth.startService(
        message.seq,
        message.get("serviceId")
      );
    });

    /**
      * Subscribes to a characteristic for a service.
      * @param serviceId
      * @param characteristicId
      */
    router.map("bluetooth.subscribe", [&](auto message, auto _, auto reply) {
      auto err = validateMessageParameters(message, {
        "characteristicId",
        "serviceId"
      });

      if (err.type != JSON::Type::Null) {
        return reply(Result::Err { message, err });
      }

      bluetooth.subscribeCharacteristic(
        message.seq,
        message.get("serviceId"),
        message.get("characteristicId")
      );
    });

    /**
      * Publishes data to a characteristic for a service.
      * @param serviceId
      * @param characteristicId
      */
    router.map("bluetooth.publish", [&](auto message, auto _, auto reply) {
      auto err = validateMessageParameters(message, {
        "characteristicId",
        "serviceId"
      });

      if (err.type != JSON::Type::Null) {
        return reply(Result::Err { message, err });
      }

      auto bytes = message.buffer.bytes;
      auto size = message.buffer.size;

      if (bytes == nullptr) {
        bytes = message.value.data();
        size = message.value.size();
      }

      bluetooth.publishCharacteristic(
        message.seq,
        bytes,
        size,
        message.get("serviceId"),
        message.get("characteristicId")
      );
    });

    /**
      * Maps a message buffer bytes to an index + sequence.
      *
      * This setup allows us to push a byte array to the bridge and
      * map it to an IPC call index and sequence pair, which is reused for an
      * actual IPC call, subsequently. This is used for systems that do not support
      * a POST/PUT body in XHR requests natively, so instead we decorate
      * `message.buffer` with already an mapped buffer.
      */
    router.map("buffer.map", false, [&](auto message, auto _, auto reply) {
      router.setMappedBuffer(
        message.index,
        message.seq,
        message.buffer.bytes,
        message.buffer.size
      );
    });

    /**
      * Returns pending data typically returned in the response of an
      * `ipc://data` IPC call intercepted by an XHR request.
      * @param id The id of the data.
      */
    router.map("data", [&](auto message, auto _, auto reply) {
      auto err = validateMessageParameters(message, {"id"});

      if (err.type != JSON::Type::Null) {
        return reply(Result::Err { message, err });
      }

      uint64_t id;
      getMessageParam(id, "id", std::stoull);

      if (!runtime.dataManager.has(id)) {
        return reply(Result::Err { message, JSON::Object::Entries {
          {"id", std::to_string(id)},
          {"message", "Data not found for given 'id'"}
        }});
      }

      auto result = Result { message.seq, message };
      result.value.data = runtime.dataManager.get(id);
      reply(result);
      //runtime.dataManager.remove(id);
    });

    /**
      * Look up an IP address by `hostname`.
      * @param hostname Host name to lookup
      * @param family IP address family to resolve [default = 0 (AF_UNSPEC)]
      * @see getaddrinfo(3)
      */
    router.map("dns.lookup", [&](auto message, auto _, auto reply) {
      auto err = validateMessageParameters(message, {"hostname"});

      if (err.type != JSON::Type::Null) {
        return reply(Result::Err { message, err });
      }

      int family = 0;
      getMessageParam(family, "family", std::stoi, "0");

      runtime.dns.lookup(
        message.seq,
        DNS::LookupOptions { message.get("hostname"), family },
        RESULT_CALLBACK(message, reply)
      );
    });

    /**
      * Checks if current user can access file at `path` with `mode`.
      * @param path
      * @param mode
      * @see access(2)
      */
    router.map("fs.access", [&](auto message, auto _, auto reply) {
      auto err = validateMessageParameters(message, {"path", "mode"});

      if (err.type != JSON::Type::Null) {
        return reply(Result::Err { message, err });
      }

      int mode = 0;
      getMessageParam(mode, "mode", std::stoi);

      runtime.fs.access(
        message.seq,
        message.get("path"),
        mode,
        RESULT_CALLBACK(message, reply)
      );
    });

    /**
      * Returns a mapping of file system constants.
      */
    router.map("fs.constants", [&](auto message, auto _, auto reply) {
      runtime.fs.constants(message.seq, RESULT_CALLBACK(message, reply));
    });

    /**
      * Changes `mode` of file at `path`.
      * @param path
      * @param mode
      * @see chmod(2)
      */
    router.map("fs.chmod", [&](auto message, auto _, auto reply) {
      auto err = validateMessageParameters(message, {"path", "mode"});

      if (err.type != JSON::Type::Null) {
        return reply(Result::Err { message, err });
      }

      int mode = 0;
      getMessageParam(mode, "mode", std::stoi);

      runtime.fs.chmod(
        message.seq,
        message.get("path"),
        mode,
        RESULT_CALLBACK(message, reply)
      );
    });

    /**
      * @TODO
      * @see chown(2)
      */
    router.map("fs.chown", [&](auto message, auto _, auto reply) {
      // TODO
    });

    /**
      * Closes underlying file descriptor handle.
      * @param id
      * @see close(2)
      */
    router.map("fs.close", [&](auto message, auto _, auto reply) {
      auto err = validateMessageParameters(message, {"id"});

      if (err.type != JSON::Type::Null) {
        return reply(Result::Err { message, err });
      }

      uint64_t id;
      getMessageParam(id, "id", std::stoull);

      runtime.fs.close(message.seq, id, RESULT_CALLBACK(message, reply));
    });

    /**
      * Closes underlying directory descriptor handle.
      * @param id
      * @see closedir(3)
      */
    router.map("fs.closedir", [&](auto message, auto _, auto reply) {
      auto err = validateMessageParameters(message, {"id"});

      if (err.type != JSON::Type::Null) {
        return reply(Result::Err { message, err });
      }

      uint64_t id;
      getMessageParam(id, "id", std::stoull);

      runtime.fs.closedir(message.seq, id, RESULT_CALLBACK(message, reply));
    });

    /**
      * Closes an open file or directory descriptor handle.
      * @param id
      * @see close(2)
      * @see closedir(3)
      */
    router.map("fs.closeOpenDescriptor", [&](auto message, auto _, auto reply) {
      auto err = validateMessageParameters(message, {"id"});

      if (err.type != JSON::Type::Null) {
        return reply(Result::Err { message, err });
      }

      uint64_t id;
      getMessageParam(id, "id", std::stoull);

      runtime.fs.closeOpenDescriptor(
        message.seq,
        id,
        RESULT_CALLBACK(message, reply)
      );
    });

    /**
      * Closes all open file and directory descriptors, optionally preserving
      * explicitly retrained descriptors.
      * @param preserveRetained (default: true)
      * @see close(2)
      * @see closedir(3)
      */
    router.map("fs.closeOpenDescriptors", [&](auto message, auto _, auto reply) {
      runtime.fs.closeOpenDescriptor(
        message.seq,
        message.get("preserveRetained") != "false",
        RESULT_CALLBACK(message, reply)
      );
    });

    /**
      * Copy file at path `src` to path `dest`.
      * @param src
      * @param dest
      * @param flags
      * @see copyfile(3)
      */
    router.map("fs.copyFile", [&](auto message, auto _, auto reply) {
      auto err = validateMessageParameters(message, {"src", "dest"});

      if (err.type != JSON::Type::Null) {
        return reply(Result::Err { message, err });
      }

      int flags = 0;
      getMessageParam(flags, "flags", std::stoi);

      runtime.fs.copyFile(
        message.seq,
        message.get("src"),
        message.get("dest"),
        flags,
        RESULT_CALLBACK(message, reply)
      );
    });

    /**
      * Computes stats for an open file descriptor.
      * @param id
      * @see stat(2)
      * @see fstat(2)
      */
    router.map("fs.fstat", [&](auto message, auto _, auto reply) {
      auto err = validateMessageParameters(message, {"id"});

      if (err.type != JSON::Type::Null) {
        return reply(Result::Err { message, err });
      }

      uint64_t id;
      getMessageParam(id, "id", std::stoull);

      runtime.fs.fstat(message.seq, id, RESULT_CALLBACK(message, reply));
    });

    /**
      * Returns all open file or directory descriptors.
      */
    router.map("fs.getOpenDescriptors", [&](auto message, auto _, auto reply) {
      runtime.fs.getOpenDescriptors(
        message.seq,
        RESULT_CALLBACK(message, reply)
      );
    });

    /**
      * Computes stats for a symbolic link at `path`.
      * @param path
      * @see stat(2)
      * @see lstat(2)
      */
    router.map("fs.lstat", [&](auto message, auto _, auto reply) {
      auto err = validateMessageParameters(message, {"path"});

      if (err.type != JSON::Type::Null) {
        return reply(Result::Err { message, err });
      }

      runtime.fs.lstat(
        message.seq,
        message.get("path"),
        RESULT_CALLBACK(message, reply)
      );
    });

    /**
      * Creates a directory at `path` with an optional mode.
      * @param path
      * @param mode
      * @see mkdir(2)
      */
    router.map("fs.mkdir", [&](auto message, auto _, auto reply) {
      auto err = validateMessageParameters(message, {"path", "mode"});

      if (err.type != JSON::Type::Null) {
        return reply(Result::Err { message, err });
      }

      int mode = 0;
      getMessageParam(mode, "mode", std::stoi);

      runtime.fs.mkdir(
        message.seq,
        message.get("path"),
        mode,
        RESULT_CALLBACK(message, reply)
      );
    });

    /**
      * Opens a file descriptor at `path` for `id` with `flags` and `mode`
      * @param id
      * @param path
      * @param flags
      * @param mode
      * @see open(2)
      */
    router.map("fs.open", [&](auto message, auto _, auto reply) {
      auto err = validateMessageParameters(message, {
        "id",
        "path",
        "flags",
        "mode"
      });

      if (err.type != JSON::Type::Null) {
        return reply(Result::Err { message, err });
      }

      uint64_t id;
      getMessageParam(id, "id", std::stoull);
      int mode = 0;
      getMessageParam(mode, "mode", std::stoi);
      int flags = 0;
      getMessageParam(flags, "flags", std::stoi);

      runtime.fs.open(
        message.seq,
        id,
        message.get("path"),
        flags,
        mode,
        RESULT_CALLBACK(message, reply)
      );
    });

    /**
      * Opens a directory descriptor at `path` for `id` with `flags` and `mode`
      * @param id
      * @param path
      * @see opendir(3)
      */
    router.map("fs.opendir", [&](auto message, auto _, auto reply) {
      auto err = validateMessageParameters(message, {"id", "path"});

      if (err.type != JSON::Type::Null) {
        return reply(Result::Err { message, err });
      }

      uint64_t id;
      getMessageParam(id, "id", std::stoull);

      runtime.fs.opendir(
        message.seq,
        id,
        message.get("path"),
        RESULT_CALLBACK(message, reply)
      );
    });

    /**
      * Reads `size` bytes at `offset` from the underlying file descriptor.
      * @param id
      * @param size
      * @param offset
      * @see read(2)
      */
    router.map("fs.read", [&](auto message, auto _, auto reply) {
      auto err = validateMessageParameters(message, {"id", "size", "offset"});

      if (err.type != JSON::Type::Null) {
        return reply(Result::Err { message, err });
      }

      uint64_t id;
      getMessageParam(id, "id", std::stoull);
      int size = 0;
      getMessageParam(size, "size", std::stoi);
      int offset = 0;
      getMessageParam(offset, "offset", std::stoi);

      runtime.fs.read(
        message.seq,
        id,
        size,
        offset,
        RESULT_CALLBACK(message, reply)
      );
    });

    /**
      * Reads next `entries` of from the underlying directory descriptor.
      * @param id
      * @param entries (default: 256)
      */
    router.map("fs.readdir", [&](auto message, auto _, auto reply) {
      auto err = validateMessageParameters(message, {"id"});

      if (err.type != JSON::Type::Null) {
        return reply(Result::Err { message, err });
      }

      uint64_t id;
      getMessageParam(id, "id", std::stoull);
      int entries = 0;
      getMessageParam(entries, "entries", std::stoi);

      runtime.fs.readdir(
        message.seq,
        id,
        entries,
        RESULT_CALLBACK(message, reply)
      );
    });

        /**
         * Marks a file or directory descriptor as retained.
         * @param id
         */
        router.map("fs.retainOpenDescriptor", [&](auto message, auto _, auto reply) {
          auto err = validateMessageParameters(message, {"id"});

          if (err.type != JSON::Type::Null) {
            return reply(Result::Err { message, err });
          }

          uint64_t id;
          getMessageParam(id, "id", std::stoull);

          runtime.fs.retainOpenDescriptor(
            message.seq,
            id,
            RESULT_CALLBACK(message, reply)
          );
        });

        /**
         * Renames file at path `src` to path `dest`.
         * @param src
         * @param dest
         * @see rename(2)
         */
        router.map("fs.rename", [&](auto message, auto _, auto reply) {
          auto err = validateMessageParameters(message, {"src", "dest"});

          if (err.type != JSON::Type::Null) {
            return reply(Result::Err { message, err });
          }

          runtime.fs.rename(
            message.seq,
            message.get("src"),
            message.get("dest"),
            RESULT_CALLBACK(message, reply)
          );
        });

        /**
         * Removes file at `path`.
         * @param path
         * @see rmdir(2)
         */
        router.map("fs.rmdir", [&](auto message, auto _, auto reply) {
          auto err = validateMessageParameters(message, {"path"});

          if (err.type != JSON::Type::Null) {
            return reply(Result::Err { message, err });
          }

          runtime.fs.rmdir(
            message.seq,
            message.get("path"),
            RESULT_CALLBACK(message, reply)
          );
        });

        /**
         * Computes stats for a file at `path`.
         * @param path
         * @see stat(2)
         */
        router.map("fs.stat", [&](auto message, auto _, auto reply) {
          auto err = validateMessageParameters(message, {"path"});

          if (err.type != JSON::Type::Null) {
            return reply(Result::Err { message, err });
          }

          runtime.fs.stat(
            message.seq,
            message.get("path"),
            RESULT_CALLBACK(message, reply)
          );
        });

        /**
         * Removes a file or empty directory at `path`.
         * @param path
         * @see unlink(2)
         */
        router.map("fs.unlink", [&](auto message, auto _, auto reply) {
          auto err = validateMessageParameters(message, {"path"});

          if (err.type != JSON::Type::Null) {
            return reply(Result::Err { message, err });
          }

          runtime.fs.unlink(
            message.seq,
            message.get("path"),
            RESULT_CALLBACK(message, reply)
          );
        });

        /**
         * Writes buffer at `message.buffer.bytes` of size `message.buffers.size`
         * at `offset` for an opened file handle.
         * @param id Handle ID for an open file descriptor
         * @param offset The offset to start writing at
         * @see write(2)
         */
        router.map("fs.write", [&](auto message, auto _, auto reply) {
          auto err = validateMessageParameters(message, {"id", "offset"});

          if (err.type != JSON::Type::Null) {
            return reply(Result::Err { message, err });
          }

          if (message.buffer.bytes == nullptr || message.buffer.size == 0) {
            auto err = JSON::Object::Entries {{ "message", "Missing buffer in message" }};
            return reply(Result::Err { message, err });
          }

          uint64_t id;
          getMessageParam(id, "id", std::stoull);
          int offset = 0;
          getMessageParam(offset, "offset", std::stoi);

          runtime.fs.write(
            message.seq,
            id,
            message.buffer.bytes,
            message.buffer.size,
            offset,
            RESULT_CALLBACK(message, reply)
          );
        });

       /**
        * Log `value to stdout` with platform dependent logger.
        * @param value
        */
        router.map("log", [&](auto message, auto _, auto reply) {
          log::info(message.value);
          reply(Result::Data { message, JSON::null });
        });

        /**
         * Read or modify the `SEND_BUFFER` or `RECV_BUFFER` for a peer socket.
         * @param id Handle ID for the buffer to read/modify
         * @param size If given, the size to set in the buffer [default = 0]
         * @param buffer The buffer to read/modify (SEND_BUFFER, RECV_BUFFER) [default = 0 (SEND_BUFFER)]
         */
        router.map("os.bufferSize", [&](auto message, auto _, auto reply) {
          auto err = validateMessageParameters(message, {"id"});

          if (err.type != JSON::Type::Null) {
            return reply(Result::Err { message, err });
          }

          uint64_t id; getMessageParam(id, "id", std::stoull);
          int buffer = 0; getMessageParam(buffer, "buffer", std::stoi, "0");
          int size = 0; getMessageParam(size, "size", std::stoi, "0");

          runtime.os.bufferSize(
            message.seq,
            id,
            size,
            buffer,
            RESULT_CALLBACK(message, reply)
          );
        });

       /**
        * Returns the platform OS.
        */
        router.map("os.platform", [&](auto message, auto _, auto reply) {
          auto result = Result { message.seq, message };
          result.value.json.data = platform.os;
          reply(result);
        });

        /**
         * Returns the platform type.
         */
        router.map("os.type", [&](auto message, auto _, auto reply) {
          auto result = Result { message.seq, message };
          result.value.json.data = platform.os;
          reply(result);
        });

        /**
         * Returns the platform architecture.
         */
        router.map("os.arch", [&](auto message, auto _, auto reply) {
          auto result = Result { message.seq, message };
          result.value.json.data = platform.arch;
          reply(result);
        });

        /**
         * Returns a mapping of network interfaces.
         */
        router.map("os.networkInterfaces", [&](auto message, auto _, auto reply) {
          runtime.os.networkInterfaces(message.seq, RESULT_CALLBACK(message, reply));
        });

        /**
         * Simply returns `pong`.
         */
        router.map("ping", [&](auto message, auto _, auto reply) {
          auto result = Result { message.seq, message };
          result.value.json.data = "pong";
          reply(result);
        });

        /**
         * Handles platform events.
         * @param value The event name [domcontentloaded]
         * @param data Optional data associated with the platform event.
         */
        router.map("platform.event", [&](auto message, auto _, auto reply) {
          auto err = validateMessageParameters(message, {"value"});

          if (err.type != JSON::Type::Null) {
            return reply(Result::Err { message, err });
          }

          runtime.platform.event(
            message.seq,
            message.value,
            message.get("data"),
            RESULT_CALLBACK(message, reply)
          );
        });

        /**
         * Requests a notification with `title` and `body` to be shown.
         * @param title
         * @param body
         */
        router.map("platform.notify", [&](auto message, auto _, auto reply) {
          auto err = validateMessageParameters(message, {"body", "title"});

          if (err.type != JSON::Type::Null) {
            return reply(Result::Err { message, err });
          }

          runtime.platform.notify(
            message.seq,
            message.get("title"),
            message.get("body"),
            RESULT_CALLBACK(message, reply)
          );
        });

        /**
         * Requests a URL to be opened externally.
         * @param value
         */
        router.map("platform.openExternal", [&](auto message, auto _, auto reply) {
          auto err = validateMessageParameters(message, {"value"});

          if (err.type != JSON::Type::Null) {
            return reply(Result::Err { message, err });
          }

          runtime.platform.openExternal(
            message.seq,
            message.value,
            RESULT_CALLBACK(message, reply)
          );
        });

        /**
         * Returns computed current working directory path.
         */
        router.map("process.cwd", [&](auto message, auto _, auto reply) {
          runtime.platform.cwd(message.seq, RESULT_CALLBACK(message, reply));
        });

        /**
         * Prints incoming message value to stdout.
         */
        router.map("stdout", [&](auto message, auto _, auto reply) {
          log::write(message.value, false);
        });

       /**
        * Prints incoming message value to stderr.
        */
        router.map("stderr", [&](auto message, auto _, auto reply) {
          log::write(message.value, true);
        });

        /**
         * Binds an UDP socket to a specified port, and optionally a host
         * address (default: 0.0.0.0).
         * @param id Handle ID of underlying socket
         * @param port Port to bind the UDP socket to
         * @param address The address to bind the UDP socket to (default: 0.0.0.0)
         * @param reuseAddr Reuse underlying UDP socket address (default: false)
         */
        router.map("udp.bind", [&](auto message, auto _, auto reply) {
          UDP::BindOptions options;
          auto err = validateMessageParameters(message, {"id", "port"});

          if (err.type != JSON::Type::Null) {
            return reply(Result::Err { message, err });
          }

          uint64_t id;
          getMessageParam(id, "id", std::stoull);
          getMessageParam(options.port, "port", std::stoi);

          options.reuseAddr = message.get("reuseAddr") == "true";
          options.address = message.get("address", "0.0.0.0");

          runtime.udp.bind(
            message.seq,
            id,
            options,
            RESULT_CALLBACK(message, reply)
          );
        });

        /**
         * Close socket handle and underlying UDP socket.
         * @param id Handle ID of underlying socket
         */
        router.map("udp.close", [&](auto message, auto _, auto reply) {
          auto err = validateMessageParameters(message, {"id"});

          if (err.type != JSON::Type::Null) {
            return reply(Result::Err { message, err });
          }

          uint64_t id;
          getMessageParam(id, "id", std::stoull);

          runtime.udp.close(message.seq, id, RESULT_CALLBACK(message, reply));
        });

        /**
         * Connects an UDP socket to a specified port, and optionally a host
         * address (default: 0.0.0.0).
         * @param id Handle ID of underlying socket
         * @param port Port to connect the UDP socket to
         * @param address The address to connect the UDP socket to (default: 0.0.0.0)
         */
        router.map("udp.connect", [&](auto message, auto _, auto reply) {
          auto err = validateMessageParameters(message, {"id", "port"});

          if (err.type != JSON::Type::Null) {
            return reply(Result::Err { message, err });
          }

          UDP::ConnectOptions options;
          uint64_t id;
          getMessageParam(id, "id", std::stoull);
          getMessageParam(options.port, "port", std::stoi);

          options.address = message.get("address", "0.0.0.0");

          runtime.udp.connect(
            message.seq,
            id,
            options,
            RESULT_CALLBACK(message, reply)
          );
        });

       /**
        * Disconnects a connected socket handle and underlying UDP socket.
        * @param id Handle ID of underlying socket
        */
        router.map("udp.disconnect", [&](auto message, auto _, auto reply) {
          auto err = validateMessageParameters(message, {"id"});

          if (err.type != JSON::Type::Null) {
            return reply(Result::Err { message, err });
          }

          uint64_t id;
          getMessageParam(id, "id", std::stoull);

          runtime.udp.disconnect(
            message.seq,
            id,
            RESULT_CALLBACK(message, reply)
          );
        });

        /**
         * Returns connected peer socket address information.
         * @param id Handle ID of underlying socket
         */
        router.map("udp.getPeerName", [&](auto message, auto _, auto reply) {
          auto err = validateMessageParameters(message, {"id"});

          if (err.type != JSON::Type::Null) {
            return reply(Result::Err { message, err });
          }

          uint64_t id;
          getMessageParam(id, "id", std::stoull);

          runtime.udp.getPeerName(
            message.seq,
            id,
            RESULT_CALLBACK(message, reply)
          );
        });

        /**
         * Returns local socket address information.
         * @param id Handle ID of underlying socket
         */
        router.map("udp.getSockName", [&](auto message, auto _, auto reply) {
          auto err = validateMessageParameters(message, {"id"});

          if (err.type != JSON::Type::Null) {
            return reply(Result::Err { message, err });
          }

          uint64_t id;
          getMessageParam(id, "id", std::stoull);

          runtime.udp.getSockName(
            message.seq,
            id,
            RESULT_CALLBACK(message, reply)
          );
        });

        /**
         * Returns socket state information.
         * @param id Handle ID of underlying socket
         */
        router.map("udp.getState", [&](auto message, auto _, auto reply) {
          auto err = validateMessageParameters(message, {"id"});

          if (err.type != JSON::Type::Null) {
            return reply(Result::Err { message, err });
          }

          uint64_t id;
          getMessageParam(id, "id", std::stoull);

          runtime.udp.getState(
            message.seq,
            id,
            RESULT_CALLBACK(message, reply)
          );
        });

        /**
         * Initializes socket handle to start receiving data from the underlying
         * socket and route through the IPC bridge to the WebView.
         * @param id Handle ID of underlying socket
         */
        router.map("udp.readStart", [&](auto message, auto _, auto reply) {
          auto err = validateMessageParameters(message, {"id"});

          if (err.type != JSON::Type::Null) {
            return reply(Result::Err { message, err });
          }

          uint64_t id;
          getMessageParam(id, "id", std::stoull);

          runtime.udp.readStart(
            message.seq,
            id,
            RESULT_CALLBACK(message, reply)
          );
        });

        /**
         * Stops socket handle from receiving data from the underlying
         * socket and routing through the IPC bridge to the WebView.
         * @param id Handle ID of underlying socket
         */
        router.map("udp.readStop", [&](auto message, auto _, auto reply) {
          auto err = validateMessageParameters(message, {"id"});

          if (err.type != JSON::Type::Null) {
            return reply(Result::Err { message, err });
          }

          uint64_t id;
          getMessageParam(id, "id", std::stoull);

          runtime.udp.readStop(
            message.seq,
            id,
            RESULT_CALLBACK(message, reply)
          );
        });

        /**
         * Broadcasts a datagram on the socket. For connectionless sockets, the
         * destination port and address must be specified. Connected sockets, on the
         * other hand, will use their associated remote endpoint, so the port and
         * address arguments must not be set.
         * @param id Handle ID of underlying socket
         * @param port The port to send data to
         * @param size The size of the bytes to send
         * @param bytes A pointer to the bytes to send
         * @param address The address to send to (default: 0.0.0.0)
         * @param ephemeral Indicates that the socket handle, if created is ephemeral and should eventually be destroyed
         */
        router.map("udp.send", [&](auto message, auto _, auto reply) {
          auto err = validateMessageParameters(message, {"id", "port"});

          if (err.type != JSON::Type::Null) {
            return reply(Result::Err { message, err });
          }

          UDP::SendOptions options;
          uint64_t id;
          getMessageParam(id, "id", std::stoull);
          getMessageParam(options.port, "port", std::stoi);

          options.size = message.buffer.size;
          options.bytes = message.buffer.bytes;
          options.address = message.get("address", "0.0.0.0");
          options.ephemeral = message.get("ephemeral") == "true";

          runtime.udp.send(
            message.seq,
            id,
            options,
            RESULT_CALLBACK(message, reply)
          );
        });
  }
}
