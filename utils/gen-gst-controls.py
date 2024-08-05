#!/usr/bin/env python3

import argparse
import re
import string
import sys
import yaml


class ControlEnum(object):
    def __init__(self, data):
        self.__data = data

    @property
    def description(self):
        """The enum description"""
        return self.__data.get('description')

    @property
    def name(self):
        """The enum name"""
        return self.__data.get('name')

    @property
    def value(self):
        """The enum value"""
        return self.__data.get('value')


class Control(object):
    def __init__(self, name, data, vendor):
        self.__name = name
        self.__data = data
        self.__enum_values = None
        self.__size = None
        self.__vendor = vendor

        enum_values = data.get('enum')
        if enum_values is not None:
            self.__enum_values = [ControlEnum(enum) for enum in enum_values]

        size = self.__data.get('size')
        if size is not None:
            if len(size) == 0:
                raise RuntimeError(f'Control `{self.__name}` size must have at least one dimension')

            # Compute the total number of elements in the array. If any of the
            # array dimension is a string, the array is variable-sized.
            num_elems = 1
            for dim in size:
                if type(dim) is str:
                    num_elems = 0
                    break

                dim = int(dim)
                if dim <= 0:
                    raise RuntimeError(f'Control `{self.__name}` size must have positive values only')

                num_elems *= dim

            self.__size = num_elems

    @property
    def description(self):
        """The control description"""
        return self.__data.get('description')

    @property
    def enum_values(self):
        """The enum values, if the control is an enumeration"""
        if self.__enum_values is None:
            return
        for enum in self.__enum_values:
            yield enum

    @property
    def is_enum(self):
        """Is the control an enumeration"""
        return self.__enum_values is not None

    @property
    def vendor(self):
        """The vendor string, or None"""
        return self.__vendor

    @property
    def name(self):
        """The control name (CamelCase)"""
        return self.__name

    @property
    def type(self):
        typ = self.__data.get('type')
        size = self.__data.get('size')

        if typ == 'string':
            return 'std::string'

        if self.__size is None:
            return typ

        if self.__size:
            return f"Span<const {typ}, {self.__size}>"
        else:
            return f"Span<const {typ}>"

    @property
    def element_type(self):
        typ = self.__data.get('type')
        return typ

    @property
    def size(self):
        return self.__size


def find_common_prefix(strings):
    prefix = strings[0]

    for string in strings[1:]:
        while string[:len(prefix)] != prefix and prefix:
            prefix = prefix[:len(prefix) - 1]
        if not prefix:
            break

    return prefix


def format_description(description, indent = 0):
    # Substitute doxygen keywords \sa (see also) and \todo
    description = re.sub(r'\\sa((?: \w+)+)',
                         lambda match: 'See also: ' + ', '.join(map(kebab_case, match.group(1).strip().split(' '))) + '.', description)
    description = re.sub(r'\\todo', 'Todo:', description)

    description = description.strip().split('\n')
    return '\n'.join([indent * '\t' + '"' + line.replace('\\', r'\\').replace('"', r'\"') + ' "' for line in description if line]).rstrip()


def snake_case(s):
    return ''.join([c.isupper() and ('_' + c.lower()) or c for c in s]).strip('_')


def kebab_case(s):
    return snake_case(s).replace('_', '-')


def indent(s, n):
    lines = s.split('\n')
    return '\n'.join([n * '\t' + line for line in lines])


def generate_cpp(controls):
    # Beginning of a GEnumValue definition for enum controls
    enum_values_start = string.Template('static const GEnumValue ${name_snake_case}_types[] = {')
    # Definition of the GEnumValue variant for each enum control variant
    # Because the description might have multiple lines it will get indented below
    enum_values_values = string.Template('''{
\tcontrols${vendor_namespace}::${name},
${description},
\t"${nick}"
},''')
    # End of a GEnumValue definition and definition of the matching _get_type() function
    enum_values_end = string.Template('''\t{0, NULL, NULL}
};

#define TYPE_${name_upper} (${name_snake_case}_get_type())
static GType ${name_snake_case}_get_type(void)
{
\tstatic GType ${name_snake_case}_type = 0;

\tif (!${name_snake_case}_type)
\t\t${name_snake_case}_type = g_enum_register_static("${name}", ${name_snake_case}_types);

\treturn ${name_snake_case}_type;
}
''')

    # Creation of the type spec for the different types of controls
    # The description (and the element_spec for the array) might have multiple lines and will get indented below
    spec_array = string.Template('''gst_param_spec_array(
\t"${spec_name}",
\t"${nick}",
${description},
${element_spec},
\t(GParamFlags) (GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
)''')
    spec_bool = string.Template('''g_param_spec_boolean(
\t"${spec_name}",
\t"${nick}",
${description},
\t${default},
\t(GParamFlags) (GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
)''')
    spec_enum = string.Template('''g_param_spec_enum(
\t"${spec_name}",
\t"${nick}",
${description},
\tTYPE_${enum},
\t${default},
\t(GParamFlags) (GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
)''')
    spec_numeric = string.Template('''g_param_spec_${gtype}(
\t"${spec_name}",
\t"${nick}",
${description},
\t${min},
\t${max},
\t${default},
\t(GParamFlags) (GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
)''')

    # The g_object_class_install_property() function call for each control
    install_property = string.Template('''\tg_object_class_install_property(
\t\tklass,
\t\tlastPropId + controls${vendor_namespace}::${name_upper},
${spec}
\t);''')

    # The _get_property() switch cases for each control
    get_property = string.Template('''case controls${vendor_namespace}::${name_upper}: {
\tauto val = controls_.get(controls${vendor_namespace}::${name});
\tauto spec = G_PARAM_SPEC_${gtype_upper}(pspec);
\tg_value_set_${gtype}(value, val.value_or(spec->default_value));
\treturn true;
}''')
    get_array_property = string.Template('''case controls${vendor_namespace}::${name_upper}: {
\tauto val = controls_.get(controls${vendor_namespace}::${name});
\tif (val) {
\t\tfor (size_t i = 0; i < val->size(); ++i) {
\t\t\tGValue v = G_VALUE_INIT;
\t\t\tg_value_init(&v, G_TYPE_${gtype_upper});
\t\t\tg_value_set_${gtype}(&v, (*val)[i]);
\t\t\tgst_value_array_append_and_take_value(value, &v);
\t\t}
\t}
\treturn true;
}''')

    # The _set_property() switch cases for each control
    set_property = string.Template('''case controls${vendor_namespace}::${name_upper}:
\tcontrols_.set(controls${vendor_namespace}::${name}, g_value_get_${gtype}(value));
\treturn true;''')
    set_array_property = string.Template('''case controls${vendor_namespace}::${name_upper}: {
\tsize_t size = gst_value_array_get_size(value);
\tstd::vector<${type}> val(size);
\tfor (size_t i = 0; i < size; ++i) {
\t\tconst GValue *v = gst_value_array_get_value(value, i);
\t\tval[i] = g_value_get_${gtype}(v);
\t}
\tcontrols_.set(controls${vendor_namespace}::${name}, Span<const ${type}, ${size}>(val.data(), size));
\treturn true;
}''')

    enum_def = []
    install_properties = []
    get_properties = []
    set_properties = []

    for ctrl in controls:
        is_array = ctrl.size is not None
        size = ctrl.size
        if size == 0:
            size = 'dynamic_extent'

        # Determine the matching glib type for each C++ type used in the controls
        gtype = ''
        if ctrl.is_enum:
            gtype = 'enum'
        elif ctrl.element_type == 'bool':
            gtype = 'boolean'
        elif ctrl.element_type == 'float':
            gtype = 'float'
        elif ctrl.element_type == 'int32_t':
            gtype = 'int'
        elif ctrl.element_type == 'int64_t':
            gtype = 'int64'
        elif ctrl.element_type == 'uint8_t':
            gtype = 'uchar'
        elif ctrl.element_type == 'Rectangle':
            # TODO: Handle Rectangle
            continue
        else:
            raise RuntimeError(f'The type `{ctrl.element_type}` is unknown')

        vendor_prefix = ''
        vendor_namespace = ''
        if ctrl.vendor != 'libcamera':
            vendor_prefix = ctrl.vendor + '-'
            vendor_namespace = '::' + ctrl.vendor

        name_snake_case = snake_case(ctrl.name)
        name_upper = name_snake_case.upper()

        info = {
            'name': ctrl.name,
            'vendor_namespace': vendor_namespace,
            'name_snake_case': name_snake_case,
            'name_upper': name_upper,
            'spec_name': vendor_prefix + kebab_case(ctrl.name),
            'nick': ''.join([c.isupper() and (' ' + c) or c for c in ctrl.name]).strip(' '),
            'description': format_description(ctrl.description, indent=1),
            'gtype': gtype,
            'gtype_upper': gtype.upper(),
            'type': ctrl.element_type,
            'size': size,
        }

        if ctrl.is_enum:
            enum_def.append(enum_values_start.substitute(info))

            common_prefix = find_common_prefix([enum.name for enum in ctrl.enum_values])

            for enum in ctrl.enum_values:
                values_info = {
                    'name': enum.name,
                    'vendor_namespace': vendor_namespace,
                    'description': format_description(enum.description, indent=1),
                    'nick': kebab_case(enum.name.removeprefix(common_prefix)),
                }
                enum_def.append(indent(enum_values_values.substitute(values_info), 1))

            enum_def.append(enum_values_end.substitute(info))

        spec = ''
        if ctrl.is_enum:
            spec = spec_enum.substitute({'enum': name_upper, 'default': 0, **info})
        elif gtype == 'boolean':
            spec = spec_bool.substitute({'default': 'false', **info})
        elif gtype == 'float':
            spec = spec_numeric.substitute({'min': '-G_MAXFLOAT', 'max': 'G_MAXFLOAT', 'default': 0, **info})
        elif gtype == 'int':
            spec = spec_numeric.substitute({'min': 'G_MININT', 'max': 'G_MAXINT', 'default': 0, **info})
        elif gtype == 'int64':
            spec = spec_numeric.substitute({'min': 'G_MININT64', 'max': 'G_MAXINT64', 'default': 0, **info})
        elif gtype == 'uchar':
            spec = spec_numeric.substitute({'min': '0', 'max': 'G_MAXUINT8', 'default': 0, **info})

        if is_array:
            spec = spec_array.substitute({'element_spec': indent(spec, 1), **info})

        install_properties.append(install_property.substitute({'spec': indent(spec, 2), **info}))

        if is_array:
            get_properties.append(indent(get_array_property.substitute(info), 1))
            set_properties.append(indent(set_array_property.substitute(info), 1))
        else:
            get_properties.append(indent(get_property.substitute(info), 1))
            set_properties.append(indent(set_property.substitute(info), 1))

    return {
        'enum_def': '\n'.join(enum_def),
        'install_properties': '\n'.join(install_properties),
        'get_properties': '\n'.join(get_properties),
        'set_properties': '\n'.join(set_properties),
    }


def fill_template(template, data):
    template = open(template, 'rb').read()
    template = template.decode('utf-8')
    template = string.Template(template)
    return template.substitute(data)


def main(argv):
    # Parse command line arguments
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', '-o', metavar='file', type=str,
                        help='Output file name. Defaults to standard output if not specified.')
    parser.add_argument('--template', '-t', dest='template', type=str, required=True,
                        help='Template file name.')
    parser.add_argument('input', type=str, nargs='+',
                        help='Input file name.')
    args = parser.parse_args(argv[1:])

    controls = []
    for input in args.input:
        data = open(input, 'rb').read()
        vendor = yaml.safe_load(data)['vendor']
        ctrls = yaml.safe_load(data)['controls']
        controls = controls + [Control(*ctrl.popitem(), vendor) for ctrl in ctrls]

    data = generate_cpp(controls)

    data = fill_template(args.template, data)

    if args.output:
        output = open(args.output, 'wb')
        output.write(data.encode('utf-8'))
        output.close()
    else:
        sys.stdout.write(data)

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
