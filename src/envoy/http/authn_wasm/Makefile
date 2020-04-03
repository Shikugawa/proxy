DOCKER_SDK=/sdk
CPP_API:=${DOCKER_SDK}
CPP_CONTEXT_LIB = ${CPP_API}/proxy_wasm_intrinsics.cc
ABSL = /root/abseil-cpp
ABSL_CPP = ${ABSL}/absl/strings/str_cat.cc ${ABSL}/absl/strings/str_split.cc ${ABSL}/absl/strings/numbers.cc ${ABSL}/absl/strings/ascii.cc

PROTO_SRCS = envoy/config/filter/http/authn/v2alpha1/config.pb.cc google/api/field_behavior.pb.cc authentication/v1alpha1/policy.pb.cc
# COMMON_SRCS = extensions/common/context.cc extensions/common/node_info_cache.cc extensions/common/util.cc

all: filter.wasm

dependenties:
	mkdir .workspace
	# clone dependenties
	git clone https://github.com/istio/api.git .workspace/api
	git clone https://github.com/googleapis/googleapis .workspace/googleapis

%.wasm %.wat: %.cc ${CPP_API}/proxy_wasm_intrinsics.h ${CPP_API}/proxy_wasm_enums.h ${CPP_API}/proxy_wasm_externs.h ${CPP_API}/proxy_wasm_api.h ${CPP_API}/proxy_wasm_intrinsics.js ${CPP_CONTEXT_LIB}
	if [ ! -d .workspace ]; then \
		make dependenties; \
	fi

	protoc .workspace/googleapis/google/api/field_behavior.proto -I.workspace/googleapis --cpp_out=.
	protoc .workspace/api/authentication/v1alpha1/policy.proto -I.workspace/api -I/usr/local/include --cpp_out=.
	protoc .workspace/api/envoy/config/filter/http/authn/v2alpha1/config.proto -I.workspace/api -I/usr/local/include --cpp_out=.

	em++ -s STANDALONE_WASM=1 -s EMIT_EMSCRIPTEN_METADATA=1 -s EXPORTED_FUNCTIONS=['_malloc'] -s ERROR_ON_UNDEFINED_SYMBOLS=0 --std=c++17 -O3 \
	-I. \
	-I${CPP_API} \
	-I${ABSL} \
	-I${CPP_API}/google/protobuf \
	-I/usr/local/include \
	$*.cc \
	${CPP_API}/proxy_wasm_intrinsics.pb.cc \
	${CPP_CONTEXT_LIB} \
	${ABSL_CPP} \
	${PROTO_SRCS} \
	${CPP_API}/libprotobuf.a \
	-o $*.wasm

	rm -rf google
	chown ${uid}.${gid} $^
