/*
 * Copyright (c) 2022, Linus Groh <linusg@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/TypeCasts.h>
#include <LibJS/Runtime/ArrayBuffer.h>
#include <LibJS/Runtime/Error.h>
#include <LibJS/Runtime/PromiseCapability.h>
#include <LibWeb/Bindings/MainThreadVM.h>
#include <LibWeb/Fetch/Body.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Bodies.h>
#include <LibWeb/FileAPI/Blob.h>
#include <LibWeb/Infra/JSON.h>
#include <LibWeb/MimeSniff/MimeType.h>
#include <LibWeb/Streams/ReadableStream.h>
#include <LibWeb/WebIDL/Promise.h>

namespace Web::Fetch {

BodyMixin::~BodyMixin() = default;

// https://fetch.spec.whatwg.org/#body-unusable
bool BodyMixin::is_unusable() const
{
    // An object including the Body interface mixin is said to be unusable if its body is non-null and its body’s stream is disturbed or locked.
    auto const& body = body_impl();
    return body.has_value() && (body->stream()->is_disturbed() || body->stream()->is_locked());
}

// https://fetch.spec.whatwg.org/#dom-body-body
JS::GCPtr<Streams::ReadableStream> BodyMixin::body() const
{
    // The body getter steps are to return null if this’s body is null; otherwise this’s body’s stream.
    auto const& body = body_impl();
    return body.has_value() ? body->stream().ptr() : nullptr;
}

// https://fetch.spec.whatwg.org/#dom-body-bodyused
bool BodyMixin::body_used() const
{
    // The bodyUsed getter steps are to return true if this’s body is non-null and this’s body’s stream is disturbed; otherwise false.
    auto const& body = body_impl();
    return body.has_value() && body->stream()->is_disturbed();
}

// https://fetch.spec.whatwg.org/#dom-body-arraybuffer
JS::NonnullGCPtr<JS::Promise> BodyMixin::array_buffer() const
{
    auto& vm = Bindings::main_thread_vm();
    auto& realm = *vm.current_realm();

    // The arrayBuffer() method steps are to return the result of running consume body with this
    // and the following step given a byte sequence bytes: return a new ArrayBuffer whose contents are bytes.
    return consume_body(realm, *this, [&realm](ByteBuffer bytes) -> WebIDL::ExceptionOr<JS::Value> {
        return JS::ArrayBuffer::create(realm, move(bytes));
    });
}

// https://fetch.spec.whatwg.org/#dom-body-blob
JS::NonnullGCPtr<JS::Promise> BodyMixin::blob() const
{
    auto& vm = Bindings::main_thread_vm();
    auto& realm = *vm.current_realm();

    // The blob() method steps are to return the result of running consume body with this
    // and the following step given a byte sequence bytes:
    return consume_body(realm, *this, [&realm, this](ByteBuffer bytes) -> WebIDL::ExceptionOr<JS::Value> {
        auto mime_type = this->mime_type_impl();

        // return a Blob whose contents are bytes and whose type attribute is this’s MIME type.
        // NOTE: If extracting the mime type returns failure, other browsers set it to an empty string - not sure if that's spec'd.
        auto mime_type_string = mime_type.has_value() ? mime_type->serialized() : DeprecatedString::empty();
        return FileAPI::Blob::create(realm, move(bytes), move(mime_type_string));
    });
}

// https://fetch.spec.whatwg.org/#dom-body-formdata
JS::NonnullGCPtr<JS::Promise> BodyMixin::form_data() const
{
    auto& vm = Bindings::main_thread_vm();
    auto& realm = *vm.current_realm();

    // The formData() method steps are to return the result of running consume body with this and
    // the following step given a byte sequence bytes:
    return consume_body(realm, *this, [this](ByteBuffer bytes) -> WebIDL::ExceptionOr<JS::Value> {
        (void)bytes;
        // switch on this’s MIME type’s essence and run the corresponding steps:
        auto mime_type = this->mime_type_impl();
        // - "multipart/form-data",
        if (mime_type.has_value() && mime_type->essence() == "multipart/form-data"sv) {
            // FIXME: 1. Parse bytes, using the value of the `boundary` parameter from mimeType, per the rules set forth in Returning Values from Forms: multipart/form-data. [RFC7578]
            // FIXME: 2. If that fails for some reason, then throw a TypeError.
            // FIXME: 3. Return a new FormData object, appending each entry, resulting from the parsing operation, to its entry list.
            return JS::js_null();
        }
        // - "application/x-www-form-urlencoded",
        else if (mime_type.has_value() && mime_type->essence() == "application/x-www-form-urlencoded"sv) {
            // FIXME: 1. Let entries be the result of parsing bytes.
            // FIXME: 2. If entries is failure, then throw a TypeError.
            // FIXME: 3. Return a new FormData object whose entry list is entries.
            return JS::js_null();
        }
        // - Otherwise
        else {
            // Throw a TypeError.
            return WebIDL::SimpleException { WebIDL::SimpleExceptionType::TypeError, "Mime type must be 'multipart/form-data' or 'application/x-www-form-urlencoded'"sv };
        }
    });
}

// https://fetch.spec.whatwg.org/#dom-body-json
JS::NonnullGCPtr<JS::Promise> BodyMixin::json() const
{
    auto& vm = Bindings::main_thread_vm();
    auto& realm = *vm.current_realm();

    // The json() method steps are to return the result of running consume body with this and parse JSON from bytes.
    return consume_body(realm, *this, [&vm](ByteBuffer bytes) -> WebIDL::ExceptionOr<JS::Value> {
        return Infra::parse_json_bytes_to_javascript_value(vm, bytes);
    });
}

// https://fetch.spec.whatwg.org/#dom-body-text
JS::NonnullGCPtr<JS::Promise> BodyMixin::text() const
{
    auto& vm = Bindings::main_thread_vm();
    auto& realm = *vm.current_realm();

    // The text() method steps are to return the result of running consume body with this and text.
    return consume_body(realm, *this, JS::SafeFunction<WebIDL::ExceptionOr<JS::Value>(AK::ByteBuffer)> { [&vm](AK::ByteBuffer bytes) -> WebIDL::ExceptionOr<JS::Value> {
        return JS::PrimitiveString::create(vm, DeprecatedString::copy(bytes));
    } });
}

// https://fetch.spec.whatwg.org/#concept-body-consume-body
JS::NonnullGCPtr<JS::Promise> consume_body(JS::Realm& realm, BodyMixin const& object, JS::SafeFunction<WebIDL::ExceptionOr<JS::Value>(AK::ByteBuffer)> convertBytesToJSValue)
{
    auto& vm = realm.vm();

    // 1. If object is unusable, then return a promise rejected with a TypeError.
    if (object.is_unusable()) {
        auto promise_capability = WebIDL::create_rejected_promise(realm, JS::TypeError::create(realm, "Body is unusable"sv));
        return verify_cast<JS::Promise>(*promise_capability->promise().ptr());
    }

    // 2. Let promise be a new promise.
    auto promise_capability = WebIDL::create_promise(realm);

    // 3. Let errorSteps given error be to reject promise with error.
    auto error_steps = [promise_capability, &vm]() {
        // NOTE: The function body->fully_read expects no arguments for this callback,
        //       even though the spec states this callback takes 'error'.
        WebIDL::reject_promise(vm, promise_capability, JS::js_null());
    };

    // 4. Let successSteps given a byte sequence data be to resolve promise with the result of running convertBytesToJSValue with data.
    //    If that threw an exception, then run errorSteps with that exception.
    auto success_steps = [promise_capability, &vm, convertBytesToJSValue = move(convertBytesToJSValue)](ByteBuffer data) {
        auto result = convertBytesToJSValue(move(data));
        if (result.is_error()) {
            // FIXME: run errorSteps with that exception.
            WebIDL::reject_promise(vm, promise_capability, JS::js_null());
        } else {
            WebIDL::resolve_promise(vm, promise_capability, result.release_value());
        }
    };

    auto const& body = object.body_impl();

    // 5. If object’s body is null, then run successSteps with an empty byte sequence.
    if (!body.has_value()) {
        success_steps(ByteBuffer {});
    }
    // 6. Otherwise, fully read object’s body given successSteps, errorSteps, and object’s relevant global object.
    else {
        // FIXME: Use object's relevant global object.
        body->fully_read(move(success_steps), move(error_steps), JS::NonnullGCPtr { realm.global_object() });
    }

    // 7. Return promise.
    return verify_cast<JS::Promise>(*promise_capability->promise().ptr());
}

}
