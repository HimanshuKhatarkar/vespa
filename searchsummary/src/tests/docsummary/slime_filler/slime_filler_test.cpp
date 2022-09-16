// Copyright Yahoo. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include <vespa/document/annotation/annotation.h>
#include <vespa/document/annotation/span.h>
#include <vespa/document/annotation/spanlist.h>
#include <vespa/document/annotation/spantree.h>
#include <vespa/document/base/documentid.h>
#include <vespa/document/datatype/documenttype.h>
#include <vespa/document/datatype/urldatatype.h>
#include <vespa/document/datatype/referencedatatype.h>
#include <vespa/document/datatype/tensor_data_type.h>
#include <vespa/document/fieldvalue/arrayfieldvalue.h>
#include <vespa/document/fieldvalue/boolfieldvalue.h>
#include <vespa/document/fieldvalue/bytefieldvalue.h>
#include <vespa/document/fieldvalue/doublefieldvalue.h>
#include <vespa/document/fieldvalue/floatfieldvalue.h>
#include <vespa/document/fieldvalue/intfieldvalue.h>
#include <vespa/document/fieldvalue/longfieldvalue.h>
#include <vespa/document/fieldvalue/predicatefieldvalue.h>
#include <vespa/document/fieldvalue/rawfieldvalue.h>
#include <vespa/document/fieldvalue/referencefieldvalue.h>
#include <vespa/document/fieldvalue/shortfieldvalue.h>
#include <vespa/document/fieldvalue/stringfieldvalue.h>
#include <vespa/document/fieldvalue/structfieldvalue.h>
#include <vespa/document/fieldvalue/tensorfieldvalue.h>
#include <vespa/document/fieldvalue/weightedsetfieldvalue.h>
#include <vespa/document/predicate/predicate.h>
#include <vespa/document/repo/configbuilder.h>
#include <vespa/document/repo/fixedtyperepo.h>
#include <vespa/eval/eval/simple_value.h>
#include <vespa/eval/eval/tensor_spec.h>
#include <vespa/eval/eval/value.h>
#include <vespa/eval/eval/value_codec.h>
#include <vespa/juniper/juniper_separators.h>
#include <vespa/searchsummary/docsummary/annotation_converter.h>
#include <vespa/searchsummary/docsummary/docsum_field_writer.h>
#include <vespa/searchsummary/docsummary/i_docsum_field_writer_factory.h>
#include <vespa/searchsummary/docsummary/i_juniper_converter.h>
#include <vespa/searchsummary/docsummary/i_string_field_converter.h>
#include <vespa/searchsummary/docsummary/linguisticsannotation.h>
#include <vespa/searchsummary/docsummary/resultconfig.h>
#include <vespa/searchsummary/docsummary/slime_filler.h>
#include <vespa/searchsummary/docsummary/slime_filler_filter.h>
#include <vespa/vespalib/data/slime/binary_format.h>
#include <vespa/vespalib/data/slime/json_format.h>
#include <vespa/vespalib/data/slime/slime.h>
#include <vespa/vespalib/data/simple_buffer.h>
#include <vespa/vespalib/gtest/gtest.h>
#include <vespa/vespalib/stllike/asciistream.h>
#include <vespa/vespalib/util/size_literals.h>
#include <vespa/config-summary.h>

using document::Annotation;
using document::ArrayFieldValue;
using document::BoolFieldValue;
using document::ByteFieldValue;
using document::DataType;
using document::Document;
using document::DocumentId;
using document::DocumentType;
using document::DocumentTypeRepo;
using document::DoubleFieldValue;
using document::Field;
using document::FieldValue;
using document::FloatFieldValue;
using document::IntFieldValue;
using document::LongFieldValue;
using document::MapFieldValue;
using document::Predicate;
using document::PredicateFieldValue;
using document::RawFieldValue;
using document::ReferenceDataType;
using document::ReferenceFieldValue;
using document::ShortFieldValue;
using document::Span;
using document::SpanList;
using document::SpanTree;
using document::StringFieldValue;
using document::StructDataType;
using document::StructFieldValue;
using document::TensorDataType;
using document::TensorFieldValue;
using document::UrlDataType;
using document::WeightedSetFieldValue;
using search::docsummary::AnnotationConverter;
using search::docsummary::IDocsumFieldWriterFactory;
using search::docsummary::IJuniperConverter;
using search::docsummary::IStringFieldConverter;
using search::docsummary::DocsumFieldWriter;
using search::docsummary::ResultConfig;
using search::docsummary::SlimeFiller;
using search::docsummary::SlimeFillerFilter;
using search::linguistics::SPANTREE_NAME;
using search::linguistics::TERM;
using vespalib::SimpleBuffer;
using vespalib::Slime;
using vespalib::eval::SimpleValue;
using vespalib::eval::TensorSpec;
using vespalib::eval::Value;
using vespalib::eval::ValueType;
using vespalib::slime::Cursor;
using vespalib::slime::JsonFormat;
using vespalib::slime::SlimeInserter;
using vespa::config::search::SummaryConfigBuilder;

namespace {

std::unique_ptr<Value>
make_tensor(const TensorSpec &spec)
{
    return SimpleValue::from_spec(spec);
}

vespalib::string
slime_to_string(const Slime& slime)
{
    SimpleBuffer buf;
    JsonFormat::encode(slime, buf, true);
    return buf.get().make_string();
}

vespalib::string
make_slime_string(vespalib::stringref value)
{
    Slime slime;
    SlimeInserter inserter(slime);
    inserter.insertString({value});
    return slime_to_string(slime);
}

vespalib::string
make_slime_data_string(vespalib::stringref data)
{
    Slime slime;
    SlimeInserter inserter(slime);
    inserter.insertData({data});
    return slime_to_string(slime);
}

vespalib::string
make_slime_tensor_string(const Value& value)
{
    vespalib::nbostream s;
    encode_value(value, s);
    return make_slime_data_string({s.peek(), s.size()});
}

class MockDocsumFieldWriterFactory : public IDocsumFieldWriterFactory
{
public:
    std::unique_ptr<DocsumFieldWriter> create_docsum_field_writer(const vespalib::string&, const vespalib::string&, const vespalib::string&, bool&) override {
        return {};
    }

};

DocumenttypesConfig
get_document_types_config()
{
    using namespace document::config_builder;
    DocumenttypesConfigBuilderHelper builder;
    const int ref_target_doctype_id = 1234;
    const int ref_type_id = 5678;
    constexpr int nested_type_id = 1235;
    builder.document(ref_target_doctype_id, "target_dummy_document",
                     Struct("target_dummy_document.header"),
                     Struct("target_dummy_document.body"));
    builder.document(42, "indexingdocument",
                     Struct("indexingdocument.header")
                     .addField("string_array", Array(DataType::T_STRING))
                     .addField("string_wset", Wset(DataType::T_STRING))
                     .addField("string_map", Map(DataType::T_STRING, DataType::T_STRING))
                     .addField("nested", Struct("nested")
                               .setId(nested_type_id)
                               .addField("a", DataType::T_INT)
                               .addField("b", DataType::T_INT)
                               .addField("c", DataType::T_INT)
                               .addField("d", nested_type_id)
                               .addField("e", nested_type_id)
                               .addField("f", nested_type_id))
                     .addField("nested_array", Array(nested_type_id))
                     .addField("nested_map", Map(DataType::T_STRING, nested_type_id))
                     .addField("ref", ref_type_id),
                   Struct("indexingdocument.body"))
        .referenceType(ref_type_id, ref_target_doctype_id);
    return builder.config();
}

class MockJuniperConverter : public IJuniperConverter
{
    vespalib::string _result;
public:
    void convert(vespalib::stringref input, vespalib::slime::Inserter&) override {
        _result = input;
    }
    const vespalib::string& get_result() const noexcept { return _result; }
};

class PassThroughStringFieldConverter : public IStringFieldConverter
{
    IJuniperConverter& _juniper_converter;
public:
    PassThroughStringFieldConverter(IJuniperConverter& juniper_converter)
        : IStringFieldConverter(),
          _juniper_converter(juniper_converter)
    {
    }
    ~PassThroughStringFieldConverter() override = default;
    void convert(const document::StringFieldValue& input, vespalib::slime::Inserter& inserter) override {
        _juniper_converter.convert(input.getValueRef(), inserter);
    }
};

}

class SlimeFillerTest : public testing::Test
{
protected:
    std::shared_ptr<const DocumentTypeRepo> _repo;
    const DocumentType*                     _document_type;
    document::FixedTypeRepo                 _fixed_repo;

    SlimeFillerTest();
    ~SlimeFillerTest() override;
    const DataType& get_data_type(const vespalib::string& name) const;
    const ReferenceDataType& get_as_ref_type(const vespalib::string& name) const;
    void set_span_tree(StringFieldValue& value, std::unique_ptr<SpanTree> tree);
    StringFieldValue make_annotated_string();
    StringFieldValue make_annotated_chinese_string();
    vespalib::string make_exp_il_annotated_string();
    vespalib::string make_exp_il_annotated_chinese_string();
    ArrayFieldValue make_array();
    WeightedSetFieldValue make_weighted_set();
    MapFieldValue make_map();
    StructFieldValue make_nested_value(int i);
    void expect_insert(const vespalib::string& exp, const FieldValue& fv, const std::vector<uint32_t>* matching_elems);
    void expect_insert(const vespalib::string& exp, const FieldValue& fv);
    void expect_insert_filtered(const vespalib::string& exp, const FieldValue& fv, const std::vector<uint32_t>& matching_elems);
    void expect_insert(const vespalib::string& exp, const FieldValue& fv, SlimeFillerFilter& filter);
    void expect_insert_callback(const vespalib::string& exp, const FieldValue& fv, bool tokenize);
};

SlimeFillerTest::SlimeFillerTest()
    : testing::Test(),
      _repo(std::make_unique<DocumentTypeRepo>(get_document_types_config())),
      _document_type(_repo->getDocumentType("indexingdocument")),
      _fixed_repo(*_repo, *_document_type)
{
}

SlimeFillerTest::~SlimeFillerTest() = default;

const DataType&
SlimeFillerTest::get_data_type(const vespalib::string& name) const
{
    const DataType *type = _repo->getDataType(*_document_type, name);
    assert(type != nullptr);
    return *type;
}

const ReferenceDataType&
SlimeFillerTest::get_as_ref_type(const vespalib::string& name) const {
    return dynamic_cast<const ReferenceDataType&>(get_data_type(name));
}

void
SlimeFillerTest::set_span_tree(StringFieldValue & value, std::unique_ptr<SpanTree> tree)
{
    StringFieldValue::SpanTrees trees;
    trees.push_back(std::move(tree));
    value.setSpanTrees(trees, _fixed_repo);
}

StringFieldValue
SlimeFillerTest::make_annotated_string()
{
    auto span_list_up = std::make_unique<SpanList>();
    auto span_list = span_list_up.get();
    auto tree = std::make_unique<SpanTree>(SPANTREE_NAME, std::move(span_list_up));
    tree->annotate(span_list->add(std::make_unique<Span>(0, 3)), *TERM);
    tree->annotate(span_list->add(std::make_unique<Span>(4, 3)),
                   Annotation(*TERM, std::make_unique<StringFieldValue>("baz")));
    StringFieldValue value("foo bar");
    set_span_tree(value, std::move(tree));
    return value;
}

StringFieldValue
SlimeFillerTest::make_annotated_chinese_string()
{
    auto span_list_up = std::make_unique<SpanList>();
    auto span_list = span_list_up.get();
    auto tree = std::make_unique<SpanTree>(SPANTREE_NAME, std::move(span_list_up));
    // These chinese characters each use 3 bytes in their UTF8 encoding.
    tree->annotate(span_list->add(std::make_unique<Span>(0, 15)), *TERM);
    tree->annotate(span_list->add(std::make_unique<Span>(15, 9)), *TERM);
    StringFieldValue value("我就是那个大灰狼");
    set_span_tree(value, std::move(tree));
    return value;
}

vespalib::string
SlimeFillerTest::make_exp_il_annotated_string()
{
    using namespace juniper::separators;
    vespalib::asciistream exp;
    exp << "foo" << unit_separator_string <<
        " " << unit_separator_string << interlinear_annotation_anchor_string <<
        "bar" << interlinear_annotation_separator_string <<
        "baz" << interlinear_annotation_terminator_string << unit_separator_string;
    return exp.str();
}

vespalib::string
SlimeFillerTest::make_exp_il_annotated_chinese_string()
{
    using namespace juniper::separators;
    vespalib::asciistream exp;
    exp << "我就是那个" << unit_separator_string <<
        "大灰狼" << unit_separator_string;
    return exp.str();
}

ArrayFieldValue
SlimeFillerTest::make_array()
{
    ArrayFieldValue array(get_data_type("Array<String>"));
    array.add(StringFieldValue("foo"));
    array.add(StringFieldValue("bar"));
    array.add(StringFieldValue("baz"));
    return array;
}

WeightedSetFieldValue
SlimeFillerTest::make_weighted_set()
{
    WeightedSetFieldValue wset(get_data_type("WeightedSet<String>"));
    wset.add(StringFieldValue("foo"), 2);
    wset.add(StringFieldValue("bar"), 4);
    wset.add(StringFieldValue("baz"), 6);
    return wset;
}

MapFieldValue
SlimeFillerTest::make_map()
{
    MapFieldValue map(get_data_type("Map<String,String>"));
    map.put(StringFieldValue("key1"), StringFieldValue("value1"));
    map.put(StringFieldValue("key2"), StringFieldValue("value2"));
    map.put(StringFieldValue("key3"), StringFieldValue("value3"));
    return map;
}

StructFieldValue
SlimeFillerTest::make_nested_value(int i)
{
    StructFieldValue nested(get_data_type("nested"));
    StructFieldValue nested2(get_data_type("nested"));
    nested.setValue("a", IntFieldValue(42 + 100 * i));
    nested.setValue("b", IntFieldValue(44 + 100 * i));
    nested.setValue("c", IntFieldValue(46 + 100 * i));
    nested2.setValue("a", IntFieldValue(62 + 100 * i));
    nested2.setValue("c", IntFieldValue(66 + 100 * i));
    nested.setValue("d", nested2);
    nested.setValue("f", nested2);
    return nested;
}

void
SlimeFillerTest::expect_insert(const vespalib::string& exp, const FieldValue& fv, const std::vector<uint32_t>* matching_elems)
{
    Slime slime;
    SlimeInserter inserter(slime);
    SlimeFiller filler(inserter, matching_elems);
    fv.accept(filler);
    auto act = slime_to_string(slime);
    EXPECT_EQ(exp, act);
}

void
SlimeFillerTest::expect_insert_filtered(const vespalib::string& exp, const FieldValue& fv, const std::vector<uint32_t>& matching_elems)
{
    expect_insert(exp, fv, &matching_elems);
}

void
SlimeFillerTest::expect_insert(const vespalib::string& exp, const FieldValue& fv)
{
    expect_insert(exp, fv, nullptr);
}

void
SlimeFillerTest::expect_insert(const vespalib::string& exp, const FieldValue& fv, SlimeFillerFilter& filter)
{
    Slime slime;
    SlimeInserter inserter(slime);
    SlimeFiller filler(inserter, nullptr, &filter);
    fv.accept(filler);
    auto act = slime_to_string(slime);
    EXPECT_EQ(exp, act);
}

void
SlimeFillerTest::expect_insert_callback(const vespalib::string& exp, const FieldValue& fv, bool tokenize)
{
    Slime slime;
    SlimeInserter inserter(slime);
    MockJuniperConverter converter;
    AnnotationConverter annotation_converter(converter);
    PassThroughStringFieldConverter passthrough_converter(converter);
    SlimeFiller filler(inserter, tokenize ? (IStringFieldConverter*) &annotation_converter : (IStringFieldConverter*) &passthrough_converter, nullptr);
    fv.accept(filler);
    auto act_null = slime_to_string(slime);
    EXPECT_EQ("null", act_null);
    auto act = converter.get_result();
    EXPECT_EQ(exp, act);
}

TEST_F(SlimeFillerTest, insert_primitive_values)
{
    {
        SCOPED_TRACE("int");
        expect_insert("42", IntFieldValue(42));
    }
    {
        SCOPED_TRACE("long");
        expect_insert("84", LongFieldValue(84));
    }
    {
        SCOPED_TRACE("short");
        expect_insert("21", ShortFieldValue(21));
    }
    {
        SCOPED_TRACE("byte");
        expect_insert("11", ByteFieldValue(11));
    }
    {
        SCOPED_TRACE("double");
        expect_insert("1.5", DoubleFieldValue(1.5));
    }
    {
        SCOPED_TRACE("float");
        expect_insert("2.5", FloatFieldValue(2.5f));
    }
    {
        SCOPED_TRACE("bool");
        expect_insert("false", BoolFieldValue(false));
        expect_insert("true", BoolFieldValue(true));
    }
}

TEST_F(SlimeFillerTest, insert_string)
{
    {
        SCOPED_TRACE("plain string");
        expect_insert(R"("Foo Bar Baz")", StringFieldValue("Foo Bar Baz"));
    }
    {
        SCOPED_TRACE("annotated string");
        auto exp = make_exp_il_annotated_string();
        expect_insert(R"("foo bar")", make_annotated_string());
    }
    {
        SCOPED_TRACE("annotated chinese string");
        auto annotated_chinese_string = make_annotated_chinese_string();
        auto exp = annotated_chinese_string.getValue();
        expect_insert(make_slime_string(exp), annotated_chinese_string);
    }
}

TEST_F(SlimeFillerTest, insert_raw)
{
    {
        SCOPED_TRACE("normal raw");
        expect_insert(make_slime_data_string("data"), RawFieldValue("data"));
    }
    {
        SCOPED_TRACE("empty raw");
        expect_insert(R"("0x")", RawFieldValue());
    }
}

TEST_F(SlimeFillerTest, insert_position)
{
    ResultConfig::set_wanted_v8_geo_positions(true);
    {
        SCOPED_TRACE("normal position");
        StructFieldValue position(get_data_type("position"));
        position.setValue("x", IntFieldValue(500000));
        position.setValue("y", IntFieldValue(750000));
        expect_insert(R"({"lat":0.75,"lng":0.5})", position);
        ResultConfig::set_wanted_v8_geo_positions(false);
        expect_insert(R"({"y":750000,"x":500000})", position);
        ResultConfig::set_wanted_v8_geo_positions(true);
    }
    {
        SCOPED_TRACE("partial position");
        StructFieldValue position(get_data_type("position"));
        position.setValue("x", IntFieldValue(500000));
        expect_insert(R"({"x":500000})", position);
    }
    {
        SCOPED_TRACE("empty position");
        StructFieldValue position(get_data_type("position"));
        expect_insert("{}", position);
    }
}

TEST_F(SlimeFillerTest, insert_uri)
{
    StructFieldValue uri(get_data_type("url"));
    uri.setValue("all", StringFieldValue("http://www.example.com:42/foobar?q#frag"));
    uri.setValue("scheme", StringFieldValue("http"));
    uri.setValue("host", StringFieldValue("www.example.com"));
    uri.setValue("port", StringFieldValue("42"));
    uri.setValue("path", StringFieldValue("foobar"));
    uri.setValue("query", StringFieldValue("q"));
    uri.setValue("fragment", StringFieldValue("frag"));
    expect_insert(R"("http://www.example.com:42/foobar?q#frag")", uri);
}

TEST_F(SlimeFillerTest, insert_predicate)
{
    auto input = std::make_unique<Slime>();
    Cursor &obj = input->setObject();
    obj.setLong(Predicate::NODE_TYPE, Predicate::TYPE_FEATURE_SET);
    obj.setString(Predicate::KEY, "foo");
    Cursor &arr = obj.setArray(Predicate::SET);
    arr.addString("bar");
    PredicateFieldValue value(std::move(input));
    expect_insert(R"("'foo' in ['bar']\n")", value);
}

TEST_F(SlimeFillerTest, insert_tensor)
{
    TensorDataType   data_type(ValueType::from_spec("tensor(x{},y{})"));
    TensorFieldValue value(data_type);
    value = make_tensor(TensorSpec("tensor(x{},y{})").add({{"x", "4"}, {"y", "5"}}, 7));
    expect_insert(make_slime_tensor_string(*value.getAsTensorPtr()), value);
    expect_insert(R"("0x")", TensorFieldValue());
}

TEST_F(SlimeFillerTest, insert_reference)
{
    {
        SCOPED_TRACE("normal reference");
        ReferenceFieldValue value(get_as_ref_type("Reference<target_dummy_document>"),
                                  DocumentId("id:ns:target_dummy_document::foo"));
        expect_insert(R"("id:ns:target_dummy_document::foo")", value);
    }
    {
        SCOPED_TRACE("empty reference");
        ReferenceFieldValue value(get_as_ref_type("Reference<target_dummy_document>"));
        expect_insert(R"("")", value);
    }
}

TEST_F(SlimeFillerTest, insert_array)
{
    auto array = make_array();
    expect_insert(R"(["foo","bar","baz"])", array);
}

TEST_F(SlimeFillerTest, insert_array_filtered)
{
    auto array = make_array();
    expect_insert_filtered(R"(["foo","bar","baz"])", array, {0, 1, 2});
    expect_insert_filtered("null", array, {});
    expect_insert_filtered(R"(["foo"])", array, {0});
    expect_insert_filtered(R"(["bar"])", array, {1});
    expect_insert_filtered(R"(["baz"])", array, {2});
    expect_insert_filtered(R"(["foo","baz"])", array, {0, 2});
    expect_insert_filtered("null", array, {0, 1, 2, 3});
}

TEST_F(SlimeFillerTest, insert_weighted_set)
{
    auto wset = make_weighted_set();
    expect_insert(R"([{"item":"foo","weight":2},{"item":"bar","weight":4},{"item":"baz","weight":6}])", wset);
}

TEST_F(SlimeFillerTest, insert_weighted_set_filtered)
{
    auto wset = make_weighted_set();
    expect_insert_filtered(R"([{"item":"foo","weight":2},{"item":"bar","weight":4},{"item":"baz","weight":6}])", wset, {0, 1, 2});
    expect_insert_filtered("null", wset, {});
    expect_insert_filtered(R"([{"item":"foo","weight":2}])", wset, {0});
    expect_insert_filtered(R"([{"item":"bar","weight":4}])", wset, {1});
    expect_insert_filtered(R"([{"item":"baz","weight":6}])", wset, {2});
    expect_insert_filtered(R"([{"item":"foo","weight":2},{"item":"baz","weight":6}])", wset, {0, 2});
    expect_insert_filtered("null", wset, {0, 1, 2, 3});
}

TEST_F(SlimeFillerTest, insert_map)
{
    auto map = make_map();
    expect_insert(R"([{"key":"key1","value":"value1"},{"key":"key2","value":"value2"},{"key":"key3","value":"value3"}])", map);
}

TEST_F(SlimeFillerTest, insert_map_filtered)
{
    auto map = make_map();
    expect_insert_filtered(R"([{"key":"key1","value":"value1"},{"key":"key2","value":"value2"},{"key":"key3","value":"value3"}])", map, {0, 1, 2});
    expect_insert_filtered("null", map, {});
    expect_insert_filtered(R"([{"key":"key1","value":"value1"}])", map, {0});
    expect_insert_filtered(R"([{"key":"key2","value":"value2"}])", map, {1});
    expect_insert_filtered(R"([{"key":"key3","value":"value3"}])", map, {2});
    expect_insert_filtered(R"([{"key":"key1","value":"value1"},{"key":"key3","value":"value3"}])", map, {0, 2});
    expect_insert_filtered("null", map, {0, 1, 2, 3});
}

TEST_F(SlimeFillerTest, insert_struct)
{
    auto nested = make_nested_value(0);
    // Field order depends on assigned field ids, cf. document::Field::calculateIdV7(), and symbol insertion order in slime
    expect_insert(R"({"f":{"c":66,"a":62},"c":46,"a":42,"b":44,"d":{"c":66,"a":62}})", nested);
    SlimeFillerFilter filter;
    filter.add("a").add("c").add("f.a").add("d");
    expect_insert(R"({"f":{"a":62},"a":42,"c":46,"d":{"a":62,"c":66}})", nested, filter);
}

TEST_F(SlimeFillerTest, insert_struct_array)
{
    ArrayFieldValue array(get_data_type("Array<nested>"));
    for (int i = 0; i < 3; ++i) {
        array.add(make_nested_value(i));
    }
    expect_insert(R"([{"f":{"c":66,"a":62},"c":46,"a":42,"b":44,"d":{"c":66,"a":62}},{"f":{"c":166,"a":162},"c":146,"a":142,"b":144,"d":{"c":166,"a":162}},{"f":{"c":266,"a":262},"c":246,"a":242,"b":244,"d":{"c":266,"a":262}}])", array);
    SlimeFillerFilter filter;
    filter.add("a").add("c").add("f.a").add("d");
    expect_insert(R"([{"f":{"a":62},"a":42,"c":46,"d":{"a":62,"c":66}},{"f":{"a":162},"a":142,"c":146,"d":{"a":162,"c":166}},{"f":{"a":262},"a":242,"c":246,"d":{"a":262,"c":266}}])", array, filter);
}

TEST_F(SlimeFillerTest, insert_struct_map)
{
    MapFieldValue map(get_data_type("Map<String,nested>"));
    for (int i = 0; i < 3; ++i) {
        vespalib::asciistream key;
        key << "key" << (i + 1);
        map.put(StringFieldValue(key.str()), make_nested_value(i));
    }
    expect_insert(R"([{"key":"key1","value":{"f":{"c":66,"a":62},"c":46,"a":42,"b":44,"d":{"c":66,"a":62}}},{"key":"key2","value":{"f":{"c":166,"a":162},"c":146,"a":142,"b":144,"d":{"c":166,"a":162}}},{"key":"key3","value":{"f":{"c":266,"a":262},"c":246,"a":242,"b":244,"d":{"c":266,"a":262}}}])", map);
    SlimeFillerFilter filter;
    filter.add("value.a").add("value.c").add("value.f.a").add("value.d");
    expect_insert(R"([{"key":"key1","value":{"f":{"a":62},"a":42,"c":46,"d":{"a":62,"c":66}}},{"key":"key2","value":{"f":{"a":162},"a":142,"c":146,"d":{"a":162,"c":166}}},{"key":"key3","value":{"f":{"a":262},"a":242,"c":246,"d":{"a":262,"c":266}}}])", map, filter);
}

TEST_F(SlimeFillerTest, insert_string_with_callback)
{
    {
        SCOPED_TRACE("plain string");
        using namespace juniper::separators;
        vespalib::string exp("Foo Bar Baz");
        StringFieldValue plain_string("Foo Bar Baz");
        expect_insert_callback(exp + unit_separator_string, plain_string, true);
        expect_insert_callback(exp, plain_string, false);
    }
    {
        SCOPED_TRACE("annotated string");
        auto exp = make_exp_il_annotated_string();
        auto annotated_string = make_annotated_string();
        expect_insert_callback(exp, annotated_string, true);
        expect_insert_callback("foo bar", annotated_string, false);
    }
    {
        SCOPED_TRACE("annotated chinese string");
        auto exp = make_exp_il_annotated_chinese_string();
        auto annotated_chinese_string = make_annotated_chinese_string();
        expect_insert_callback(exp, annotated_chinese_string, true);
        expect_insert_callback(annotated_chinese_string.getValueRef(), annotated_chinese_string, false);
    }
}

GTEST_MAIN_RUN_ALL_TESTS()