<?php
/**
 * Automatically generated by running "php schema.php icu_uspoof".
 *
 * You may modify the file, but re-running schema.php against this file will
 * standardize the format without losing your schema changes. It does lose
 * any changes that are not part of schema. Use "note" field to comment on
 * schema itself, and "note" fields are not used in any code generation but
 * only staying within this file.
 *
 * @nolint
 */
///////////////////////////////////////////////////////////////////////////////
// Preamble: C++ code inserted at beginning of ext_{name}.h

DefinePreamble(<<<CPP

// Avoid dragging in the icu namespace.
#ifndef U_USING_ICU_NAMESPACE
#define U_USING_ICU_NAMESPACE 0
#endif

#include "unicode/uspoof.h"
#include "unicode/utypes.h"
CPP
);

///////////////////////////////////////////////////////////////////////////////
// Constants
//
// array (
//   'name' => name of the constant
//   'type' => type of the constant
//   'note' => additional note about this constant's schema
// )


///////////////////////////////////////////////////////////////////////////////
// Functions
//
// array (
//   'name'   => name of the function
//   'desc'   => description of the function's purpose
//   'flags'  => attributes of the function, see base.php for possible values
//   'opt'    => optimization callback function's name for compiler
//   'note'   => additional note about this function's schema
//   'return' =>
//      array (
//        'type'  => return type, use Reference for ref return
//        'desc'  => description of the return value
//      )
//   'args'   => arguments
//      array (
//        'name'  => name of the argument
//        'type'  => type of the argument, use Reference for output parameter
//        'value' => default value of the argument
//        'desc'  => description of the argument
//      )
//   'taint_observer' => taint propagation information
//     array (
//       'set_mask' => which bits to set automatically
//       'clear_mask' => which bits to clear automatically
//     )
// )


///////////////////////////////////////////////////////////////////////////////
// Classes
//
// BeginClass
// array (
//   'name'   => name of the class
//   'desc'   => description of the class's purpose
//   'flags'  => attributes of the class, see base.php for possible values
//   'note'   => additional note about this class's schema
//   'parent' => parent class name, if any
//   'ifaces' => array of interfaces tihs class implements
//   'bases'  => extra internal and special base classes this class requires
//   'footer' => extra C++ inserted at end of class declaration
// )
//
// DefineConstant(..)
// DefineConstant(..)
// ...
// DefineFunction(..)
// DefineFunction(..)
// ...
// DefineProperty
// DefineProperty
//
// array (
//   'name'  => name of the property
//   'type'  => type of the property
//   'flags' => attributes of the property
//   'desc'  => description of the property
//   'note'  => additional note about this property's schema
// )
//
// EndClass()

///////////////////////////////////////////////////////////////////////////////

BeginClass(
  array(
    'name'   => "SpoofChecker",
    'desc'   => "Unicode Security and Spoofing Detection (see http://icu-project.org/apiref/icu4c/uspoof_8h.html for details)",
    'flags'  =>  HasDocComment,
    'footer' => <<<EOT

  private: USpoofChecker *m_spoof_checker;
EOT
,
  ));

DefineConstant(
  array(
    'name'   => "SINGLE_SCRIPT_CONFUSABLE",
    'type'   => Int64,
  ));

DefineConstant(
  array(
    'name'   => "MIXED_SCRIPT_CONFUSABLE",
    'type'   => Int64,
  ));

DefineConstant(
  array(
    'name'   => "WHOLE_SCRIPT_CONFUSABLE",
    'type'   => Int64,
  ));

DefineConstant(
  array(
    'name'   => "ANY_CASE",
    'type'   => Int64,
  ));

DefineConstant(
  array(
    'name'   => "SINGLE_SCRIPT",
    'type'   => Int64,
  ));

DefineConstant(
  array(
    'name'   => "INVISIBLE",
    'type'   => Int64,
  ));

DefineConstant(
  array(
    'name'   => "CHAR_LIMIT",
    'type'   => Int64,
  ));

DefineFunction(
  array(
    'name'   => "__construct",
    'desc'   => "Creates a spoof checker that checks for visually confusing characters in a string.  By default, runs the following tests: SINGLE_SCRIPT_CONFUSABLE, MIXED_SCRIPT_CONFUSABLE, WHOLE_SCRIPT_CONFUSABLE, ANY_CASE, INVISIBLE.",
    'flags'  =>  HasDocComment,
    'return' => array(
      'type'   => null,
    ),
  ));

DefineFunction(
  array(
    'name'   => "isSuspicious",
    'desc'   => "Check the specified UTF-8 string for possible security or spoofing issues.",
    'flags'  =>  HasDocComment,
    'return' => array(
      'type'   => Boolean,
      'desc'   => "Returns TRUE if the string has possible security or spoofing issues, FALSE otherwise.",
    ),
    'args'   => array(
      array(
        'name'   => "text",
        'type'   => String,
        'desc'   => "A UTF-8 string to be checked for possible security issues.",
      ),
      array(
        'name'   => "issuesFound",
        'type'   => Variant | Reference,
        'value'  => "null",
        'desc'   => "If passed, this will hold an integer value with bits set for any potential security or spoofing issues detected. Zero is returned if no issues are found with the input string.",
      ),
    ),
  ));

DefineFunction(
  array(
    'name'   => "areConfusable",
    'desc'   => "Check the whether two specified UTF-8 strings are visually confusable. The types of confusability to be tested - single script, mixed script, or whole script - are determined by the check options set for this instance.",
    'flags'  =>  HasDocComment,
    'return' => array(
      'type'   => Boolean,
      'desc'   => "Returns TRUE if the two strings are confusable, FALSE otherwise.",
    ),
    'args'   => array(
      array(
        'name'   => "s1",
        'type'   => String,
        'desc'   => "The first of the two UTF-8 strings to be compared for confusability.",
      ),
      array(
        'name'   => "s2",
        'type'   => String,
        'desc'   => "The second of the two UTF-8 strings to be compared for confusability.",
      ),
      array(
        'name'   => "issuesFound",
        'type'   => Variant | Reference,
        'value'  => "null",
        'desc'   => "If passed, this will hold an integer value with bit(s) set corresponding to the type of confusability found, as defined by the constant values stored in this class. Zero is returned if the strings are not confusable.",
      ),
    ),
  ));

DefineFunction(
  array(
    'name'   => "setAllowedLocales",
    'desc'   => "Limit characters that are acceptable in identifiers being checked to those normally used with the languages associated with the specified locales.",
    'flags'  =>  HasDocComment,
    'return' => array(
      'type'   => null,
    ),
    'args'   => array(
      array(
        'name'   => "localesList",
        'type'   => String,
        'desc'   => "A list of locales, from which the language and associated script are extracted. The locales are comma-separated if there is more than one. White space may not appear within an individual locale, but is ignored otherwise. The locales are syntactically like those from the HTTP Accept-Language header. If the localesList is empty, no restrictions will be placed on the allowed characters."
      ),
    ),
  ));

DefineFunction(
  array(
    'name'   => "setChecks",
    'desc'   => "Specify the set of checks that will be performed by the check function",
    'flags'  =>  HasDocComment,
    'return' => array(
      'type'   => null,
    ),
    'args'   => array(
      array(
        'name'   => "checks",
        'type'   => Int32,
        'desc'   => "The set of checks that this spoof checker will perform. The value is a bit set, obtained by OR-ing together the constant values in this class.",
      ),
    ),
  ));

EndClass(
);
