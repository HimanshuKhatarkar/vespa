package feed

import (
	"bytes"
	"encoding/json"
	"io"
	"net/url"
	"reflect"
	"strings"
	"testing"
)

func ptr[T any](v T) *T { return &v }

func mustParseDocument(d Document) Document {
	if err := parseDocument(&d); err != nil {
		panic(err)
	}
	return d
}

func TestParseDocumentId(t *testing.T) {
	tests := []struct {
		in   string
		out  DocumentId
		fail bool
	}{
		{"id:ns:type::user",
			DocumentId{
				Namespace:    "ns",
				Type:         "type",
				UserSpecific: "user",
			},
			false,
		},
		{"id:ns:type:n=123:user",
			DocumentId{
				Namespace:    "ns",
				Type:         "type",
				Number:       ptr(int64(123)),
				UserSpecific: "user",
			},
			false,
		},
		{"id:ns:type:g=foo:user",
			DocumentId{
				Namespace:    "ns",
				Type:         "type",
				Group:        "foo",
				UserSpecific: "user",
			},
			false,
		},
		{"id:ns:type::user::specific",
			DocumentId{
				Namespace:    "ns",
				Type:         "type",
				UserSpecific: "user::specific",
			},
			false,
		},
		{"id:ns:type:::",
			DocumentId{
				Namespace:    "ns",
				Type:         "type",
				UserSpecific: ":",
			},
			false,
		},
		{"", DocumentId{}, true},
		{"foobar", DocumentId{}, true},
		{"idd:ns:type:user", DocumentId{}, true},
		{"id:ns::user", DocumentId{}, true},
		{"id::type:user", DocumentId{}, true},
		{"id:ns:type:g=:user", DocumentId{}, true},
		{"id:ns:type:n=:user", DocumentId{}, true},
		{"id:ns:type:n=foo:user", DocumentId{}, true},
		{"id:ns:type::", DocumentId{}, true},
	}
	for i, tt := range tests {
		parsed, err := ParseDocumentId(tt.in)
		if err == nil && tt.fail {
			t.Errorf("#%d: expected error for ParseDocumentId(%q), but got none", i, tt.in)
		}
		if err != nil && !tt.fail {
			t.Errorf("#%d: got unexpected error for ParseDocumentId(%q) = (_, %v)", i, tt.in, err)
		}
		if !parsed.Equal(tt.out) {
			t.Errorf("#%d: ParseDocumentId(%q) = (%s, _), want %s", i, tt.in, parsed, tt.out)
		}
	}
}

func feedInput(jsonl bool) string {
	operations := []string{
		`
{
  "put": "id:ns:type::doc1",
  "fields": {"foo": "123"}
}`,
		`
{
  "put": "id:ns:type::doc2",
  "fields": {"bar": "456"}
}`,
		`
{
  "remove": "id:ns:type::doc1"
}
`}
	if jsonl {
		return strings.Join(operations, "\n")
	}
	return "   \n[" + strings.Join(operations, ",") + "]"
}

func testDocumentDecoder(t *testing.T, jsonLike string) {
	t.Helper()
	r := NewDecoder(strings.NewReader(jsonLike))
	want := []Document{
		mustParseDocument(Document{PutId: "id:ns:type::doc1", Fields: json.RawMessage(`{"foo": "123"}`)}),
		mustParseDocument(Document{PutId: "id:ns:type::doc2", Fields: json.RawMessage(`{"bar": "456"}`)}),
		mustParseDocument(Document{RemoveId: "id:ns:type::doc1", Fields: json.RawMessage(nil)}),
	}
	got := []Document{}
	for {
		doc, err := r.Decode()
		if err == io.EOF {
			break
		}
		if err != nil {
			t.Fatal(err)
		}
		got = append(got, doc)
	}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("got %+v, want %+v", got, want)
	}
}

func TestDocumentDecoder(t *testing.T) {
	testDocumentDecoder(t, feedInput(false))
	testDocumentDecoder(t, feedInput(true))

	jsonLike := `
{
  "put": "id:ns:type::doc1",
  "fields": {"foo": "123"}
}
{
  "put": "id:ns:type::doc1",
  "fields": {"foo": "invalid
}
`
	r := NewDecoder(strings.NewReader(jsonLike))
	_, err := r.Decode() // first object is valid
	if err != nil {
		t.Errorf("unexpected error: %s", err)
	}
	_, err = r.Decode()
	wantErr := "invalid json at byte offset 60: invalid character '\\n' in string literal"
	if err.Error() != wantErr {
		t.Errorf("want error %q, got %q", wantErr, err.Error())
	}
}

func TestDocumentIdURLPath(t *testing.T) {
	tests := []struct {
		in  DocumentId
		out string
	}{
		{
			DocumentId{
				Namespace:    "ns-with-/",
				Type:         "type-with-/",
				UserSpecific: "user",
			},
			"/document/v1/ns-with-%2F/type-with-%2F/docid/user",
		},
		{
			DocumentId{
				Namespace:    "ns",
				Type:         "type",
				Number:       ptr(int64(123)),
				UserSpecific: "user",
			},
			"/document/v1/ns/type/number/123/user",
		},
		{
			DocumentId{
				Namespace:    "ns",
				Type:         "type",
				Group:        "foo",
				UserSpecific: "user",
			},
			"/document/v1/ns/type/group/foo/user",
		},
		{
			DocumentId{
				Namespace:    "ns",
				Type:         "type",
				UserSpecific: "user::specific",
			},
			"/document/v1/ns/type/docid/user::specific",
		},
		{
			DocumentId{
				Namespace:    "ns",
				Type:         "type",
				UserSpecific: ":",
			},
			"/document/v1/ns/type/docid/:",
		},
	}
	for i, tt := range tests {
		path := tt.in.URLPath()
		if path != tt.out {
			t.Errorf("#%d: documentPath(%q) = %s, want %s", i, tt.in, path, tt.out)
		}
	}
}

func TestDocumentURL(t *testing.T) {
	tests := []struct {
		in     Document
		method string
		url    string
	}{
		{
			mustParseDocument(Document{
				IdString: "id:ns:type::user",
			}),
			"POST",
			"https://example.com/document/v1/ns/type/docid/user?foo=ba%2Fr",
		},
		{
			mustParseDocument(Document{
				UpdateId:  "id:ns:type::user",
				Create:    true,
				Condition: "false",
			}),
			"PUT",
			"https://example.com/document/v1/ns/type/docid/user?condition=false&create=true&foo=ba%2Fr",
		},
		{
			mustParseDocument(Document{
				RemoveId: "id:ns:type::user",
			}),
			"DELETE",
			"https://example.com/document/v1/ns/type/docid/user?foo=ba%2Fr",
		},
	}
	for i, tt := range tests {
		moreParams := url.Values{}
		moreParams.Set("foo", "ba/r")
		method, u, err := tt.in.FeedURL("https://example.com", moreParams)
		if err != nil {
			t.Errorf("#%d: got unexpected error = %s, want none", i, err)
		}
		if u.String() != tt.url || method != tt.method {
			t.Errorf("#%d: URL() = (%s, %s), want (%s, %s)", i, method, u.String(), tt.method, tt.url)
		}
	}
}

func TestDocumentBody(t *testing.T) {
	doc := Document{Fields: json.RawMessage([]byte(`{"foo": "123"}`))}
	got := doc.Body()
	want := []byte(`{"fields":{"foo": "123"}}`)
	if !bytes.Equal(got, want) {
		t.Errorf("got %q, want %q", got, want)
	}
}
