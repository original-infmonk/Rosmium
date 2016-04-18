#ifndef OSMIUM_IO_DETAIL_XML_OUTPUT_FORMAT_HPP
#define OSMIUM_IO_DETAIL_XML_OUTPUT_FORMAT_HPP

/*

This file is part of Osmium (http://osmcode.org/libosmium).

Copyright 2013-2015 Jochen Topf <jochen@topf.org> and others (see README).

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdio>
#include <future>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <osmium/io/detail/output_format.hpp>
#include <osmium/io/file.hpp>
#include <osmium/io/file_format.hpp>
#include <osmium/io/header.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/memory/collection.hpp>
#include <osmium/osm/box.hpp>
#include <osmium/osm/changeset.hpp>
#include <osmium/osm/item_type.hpp>
#include <osmium/osm/location.hpp>
#include <osmium/osm/node.hpp>
#include <osmium/osm/object.hpp>
#include <osmium/osm/relation.hpp>
#include <osmium/osm/tag.hpp>
#include <osmium/osm/timestamp.hpp>
#include <osmium/osm/types.hpp>
#include <osmium/osm/way.hpp>
#include <osmium/thread/pool.hpp>
#include <osmium/visitor.hpp>

namespace osmium {

    namespace io {

        namespace detail {

            struct XMLWriteError {};

            struct xml_output_options {

                /// Should metadata of objects be added?
                bool add_metadata;

                /// Should the visible flag be added to all OSM objects?
                bool add_visible_flag;

                /**
                 * Should <create>, <modify>, <delete> "operations" be added?
                 * (This is used for .osc files.)
                 */
                bool use_change_ops;

            };

            class XMLOutputBlock : public OutputBlock {

                // operation (create, modify, delete) for osc files
                enum class operation {
                    op_none   = 0,
                    op_create = 1,
                    op_modify = 2,
                    op_delete = 3
                }; // enum class operation

                operation m_last_op {operation::op_none};

                xml_output_options m_options;

                void write_spaces(int num) {
                    for (; num != 0; --num) {
                        *m_out += ' ';
                    }
                }

                int prefix_spaces() {
                    return m_options.use_change_ops ? 4 : 2;
                }

                void write_prefix() {
                    write_spaces(prefix_spaces());
                }

                void write_meta(const osmium::OSMObject& object) {
                    output_formatted(" id=\"%" PRId64 "\"", object.id());

                    if (m_options.add_metadata) {
                        if (object.version()) {
                            output_formatted(" version=\"%d\"", object.version());
                        }

                        if (object.timestamp()) {
                            *m_out += " timestamp=\"";
                            *m_out += object.timestamp().to_iso();
                            *m_out += "\"";
                        }

                        if (!object.user_is_anonymous()) {
                            output_formatted(" uid=\"%d\" user=\"", object.uid());
                            append_xml_encoded_string(*m_out, object.user());
                            *m_out += "\"";
                        }

                        if (object.changeset()) {
                            output_formatted(" changeset=\"%d\"", object.changeset());
                        }

                        if (m_options.add_visible_flag) {
                            if (object.visible()) {
                                *m_out += " visible=\"true\"";
                            } else {
                                *m_out += " visible=\"false\"";
                            }
                        }
                    }
                }

                void write_tags(const osmium::TagList& tags, int spaces) {
                    for (const auto& tag : tags) {
                        write_spaces(spaces);
                        *m_out += "  <tag k=\"";
                        append_xml_encoded_string(*m_out, tag.key());
                        *m_out += "\" v=\"";
                        append_xml_encoded_string(*m_out, tag.value());
                        *m_out += "\"/>\n";
                    }
                }

                void write_discussion(const osmium::ChangesetDiscussion& comments) {
                    for (const auto& comment : comments) {
                        output_formatted("   <comment uid=\"%d\" user=\"", comment.uid());
                        append_xml_encoded_string(*m_out, comment.user());
                        *m_out += "\" date=\"";
                        *m_out += comment.date().to_iso();
                        *m_out += "\">\n";
                        *m_out += "    <text>";
                        append_xml_encoded_string(*m_out, comment.text());
                        *m_out += "</text>\n   </comment>\n";
                    }
                    *m_out += "  </discussion>\n";
                }

                void open_close_op_tag(const operation op = operation::op_none) {
                    if (op == m_last_op) {
                        return;
                    }

                    switch (m_last_op) {
                        case operation::op_none:
                            break;
                        case operation::op_create:
                            *m_out += "  </create>\n";
                            break;
                        case operation::op_modify:
                            *m_out += "  </modify>\n";
                            break;
                        case operation::op_delete:
                            *m_out += "  </delete>\n";
                            break;
                    }

                    switch (op) {
                        case operation::op_none:
                            break;
                        case operation::op_create:
                            *m_out += "  <create>\n";
                            break;
                        case operation::op_modify:
                            *m_out += "  <modify>\n";
                            break;
                        case operation::op_delete:
                            *m_out += "  <delete>\n";
                            break;
                    }

                    m_last_op = op;
                }

            public:

                XMLOutputBlock(osmium::memory::Buffer&& buffer, const xml_output_options& options) :
                    OutputBlock(std::move(buffer)),
                    m_options(options) {
                }

                XMLOutputBlock(const XMLOutputBlock&) = default;
                XMLOutputBlock& operator=(const XMLOutputBlock&) = default;

                XMLOutputBlock(XMLOutputBlock&&) = default;
                XMLOutputBlock& operator=(XMLOutputBlock&&) = default;

                ~XMLOutputBlock() noexcept = default;

                std::string operator()() {
                    osmium::apply(m_input_buffer->cbegin(), m_input_buffer->cend(), *this);

                    if (m_options.use_change_ops) {
                        open_close_op_tag();
                    }

                    std::string out;
                    using std::swap;
                    swap(out, *m_out);

                    return out;
                }

                void node(const osmium::Node& node) {
                    if (m_options.use_change_ops) {
                        open_close_op_tag(node.visible() ? (node.version() == 1 ? operation::op_create : operation::op_modify) : operation::op_delete);
                    }

                    write_prefix();
                    *m_out += "<node";

                    write_meta(node);

                    if (node.location()) {
                        *m_out += " lat=\"";
                        osmium::util::double2string(std::back_inserter(*m_out), node.location().lat_without_check(), 7);
                        *m_out += "\" lon=\"";
                        osmium::util::double2string(std::back_inserter(*m_out), node.location().lon_without_check(), 7);
                        *m_out += "\"";
                    }

                    if (node.tags().empty()) {
                        *m_out += "/>\n";
                        return;
                    }

                    *m_out += ">\n";

                    write_tags(node.tags(), prefix_spaces());

                    write_prefix();
                    *m_out += "</node>\n";
                }

                void way(const osmium::Way& way) {
                    if (m_options.use_change_ops) {
                        open_close_op_tag(way.visible() ? (way.version() == 1 ? operation::op_create : operation::op_modify) : operation::op_delete);
                    }

                    write_prefix();
                    *m_out += "<way";
                    write_meta(way);

                    if (way.tags().empty() && way.nodes().empty()) {
                        *m_out += "/>\n";
                        return;
                    }

                    *m_out += ">\n";

                    for (const auto& node_ref : way.nodes()) {
                        write_prefix();
                        output_formatted("  <nd ref=\"%" PRId64 "\"/>\n", node_ref.ref());
                    }

                    write_tags(way.tags(), prefix_spaces());

                    write_prefix();
                    *m_out += "</way>\n";
                }

                void relation(const osmium::Relation& relation) {
                    if (m_options.use_change_ops) {
                        open_close_op_tag(relation.visible() ? (relation.version() == 1 ? operation::op_create : operation::op_modify) : operation::op_delete);
                    }

                    write_prefix();
                    *m_out += "<relation";
                    write_meta(relation);

                    if (relation.tags().empty() && relation.members().empty()) {
                        *m_out += "/>\n";
                        return;
                    }

                    *m_out += ">\n";

                    for (const auto& member : relation.members()) {
                        write_prefix();
                        *m_out += "  <member type=\"";
                        *m_out += item_type_to_name(member.type());
                        output_formatted("\" ref=\"%" PRId64 "\" role=\"", member.ref());
                        append_xml_encoded_string(*m_out, member.role());
                        *m_out += "\"/>\n";
                    }

                    write_tags(relation.tags(), prefix_spaces());

                    write_prefix();
                    *m_out += "</relation>\n";
                }

                void changeset(const osmium::Changeset& changeset) {
                    *m_out += " <changeset";

                    output_formatted(" id=\"%" PRId32 "\"", changeset.id());

                    if (changeset.created_at()) {
                        *m_out += " created_at=\"";
                        *m_out += changeset.created_at().to_iso();
                        *m_out += "\"";
                    }

                    if (changeset.closed_at()) {
                        *m_out += " closed_at=\"";
                        *m_out += changeset.closed_at().to_iso();
                        *m_out += "\" open=\"false\"";
                    } else {
                        *m_out += " open=\"true\"";
                    }

                    if (!changeset.user_is_anonymous()) {
                        *m_out += " user=\"";
                        append_xml_encoded_string(*m_out, changeset.user());
                        output_formatted("\" uid=\"%d\"", changeset.uid());
                    }

                    if (changeset.bounds()) {
                        output_formatted(" min_lat=\"%.7f\"", changeset.bounds().bottom_left().lat_without_check());
                        output_formatted(" min_lon=\"%.7f\"", changeset.bounds().bottom_left().lon_without_check());
                        output_formatted(" max_lat=\"%.7f\"", changeset.bounds().top_right().lat_without_check());
                        output_formatted(" max_lon=\"%.7f\"", changeset.bounds().top_right().lon_without_check());
                    }

                    output_formatted(" num_changes=\"%" PRId32 "\"", changeset.num_changes());
                    output_formatted(" comments_count=\"%" PRId32 "\"", changeset.num_comments());

                    // If there are no tags and no comments, we can close the
                    // tag right here and are done.
                    if (changeset.tags().empty() && changeset.num_comments() == 0) {
                        *m_out += "/>\n";
                        return;
                    }

                    *m_out += ">\n";

                    write_tags(changeset.tags(), 0);

                    if (changeset.num_comments() > 0) {
                        *m_out += "  <discussion>\n";
                        write_discussion(changeset.discussion());
                    }

                    *m_out += " </changeset>\n";
                }

            }; // class XMLOutputBlock

            class XMLOutputFormat : public osmium::io::detail::OutputFormat, public osmium::handler::Handler {

                xml_output_options m_options;

            public:

                XMLOutputFormat(const osmium::io::File& file, future_string_queue_type& output_queue) :
                    OutputFormat(output_queue),
                    m_options() {
                    m_options.add_metadata     = file.is_not_false("add_metadata");
                    m_options.use_change_ops   = file.is_true("xml_change_format");
                    m_options.add_visible_flag = (file.has_multiple_object_versions() || file.is_true("force_visible_flag")) && !m_options.use_change_ops;
                }

                XMLOutputFormat(const XMLOutputFormat&) = delete;
                XMLOutputFormat& operator=(const XMLOutputFormat&) = delete;

                ~XMLOutputFormat() noexcept final = default;

                void write_header(const osmium::io::Header& header) final {
                    std::string out = "<?xml version='1.0' encoding='UTF-8'?>\n";

                    if (m_options.use_change_ops) {
                        out += "<osmChange version=\"0.6\" generator=\"";
                    } else {
                        out += "<osm version=\"0.6\"";

                        std::string xml_josm_upload = header.get("xml_josm_upload");
                        if (xml_josm_upload == "true" || xml_josm_upload == "false") {
                            out += " upload=\"";
                            out += xml_josm_upload;
                            out += "\"";
                        }
                        out += " generator=\"";
                    }
                    append_xml_encoded_string(out, header.get("generator").c_str());
                    out += "\">\n";

                    for (const auto& box : header.boxes()) {
                        out += "  <bounds";
                        append_printf_formatted_string(out, " minlon=\"%.7f\"", box.bottom_left().lon());
                        append_printf_formatted_string(out, " minlat=\"%.7f\"", box.bottom_left().lat());
                        append_printf_formatted_string(out, " maxlon=\"%.7f\"", box.top_right().lon());
                        append_printf_formatted_string(out, " maxlat=\"%.7f\"/>\n", box.top_right().lat());
                    }

                    send_to_output_queue(std::move(out));
                }

                void write_buffer(osmium::memory::Buffer&& buffer) final {
                    m_output_queue.push(osmium::thread::Pool::instance().submit(XMLOutputBlock{std::move(buffer), m_options}));
                }

                void write_end() final {
                    std::string out;

                    if (m_options.use_change_ops) {
                        out += "</osmChange>\n";
                    } else {
                        out += "</osm>\n";
                    }

                    send_to_output_queue(std::move(out));
                }

            }; // class XMLOutputFormat

            // we want the register_output_format() function to run, setting
            // the variable is only a side-effect, it will never be used
            const bool registered_xml_output = osmium::io::detail::OutputFormatFactory::instance().register_output_format(osmium::io::file_format::xml,
                [](const osmium::io::File& file, future_string_queue_type& output_queue) {
                    return new osmium::io::detail::XMLOutputFormat(file, output_queue);
            });

            // dummy function to silence the unused variable warning from above
            inline bool get_registered_xml_output() noexcept {
                return registered_xml_output;
            }

        } // namespace detail

    } // namespace io

} // namespace osmium

#endif // OSMIUM_IO_DETAIL_XML_OUTPUT_FORMAT_HPP