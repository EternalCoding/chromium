// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var g_browserBridge;
var g_mainView;

/**
 * Main entry point called once the page has loaded.
 */
function onLoad() {
  g_browserBridge = new BrowserBridge();
  g_mainView = new MainView();

  // Ask the browser to send us the current data.
  g_browserBridge.sendGetData();
}

document.addEventListener('DOMContentLoaded', onLoad);

/**
 * This class provides a "bridge" for communicating between the javascript and
 * the browser. Used as a singleton.
 */
var BrowserBridge = (function() {
  'use strict';

  /**
   * @constructor
   */
  function BrowserBridge() {
  }

  BrowserBridge.prototype = {
    //--------------------------------------------------------------------------
    // Messages sent to the browser
    //--------------------------------------------------------------------------

    sendGetData: function() {
      chrome.send('getData');
    },

    sendResetData: function() {
      chrome.send('resetData');
    },

    //--------------------------------------------------------------------------
    // Messages received from the browser.
    //--------------------------------------------------------------------------

    receivedData: function(data) {
      g_mainView.addData(data);
    },
  };

  return BrowserBridge;
})();

/**
 * This class handles the presentation of our tracking view. Used as a
 * singleton.
 */
var MainView = (function() {
  'use strict';

  // --------------------------------------------------------------------------
  // Important IDs in the HTML document
  // --------------------------------------------------------------------------

  // The search box to filter results.
  var FILTER_SEARCH_ID = 'filter-search';

  // The container node to put all the "Group by" dropdowns into.
  var GROUP_BY_CONTAINER_ID = 'group-by-container';

  // The container node to put all the "Sort by" dropdowns into.
  var SORT_BY_CONTAINER_ID = 'sort-by-container';

  // The DIV to put all the tables into.
  var RESULTS_DIV_ID = 'results-div';

  // The container node to put all the column checkboxes into.
  var COLUMN_TOGGLES_CONTAINER_ID = 'column-toggles-container';

  // The anchor which toggles visibility of column checkboxes.
  var EDIT_COLUMNS_LINK_ID = 'edit-columns-link';

  // The container node to show/hide when toggling the column checkboxes.
  var EDIT_COLUMNS_ROW = 'edit-columns-row';

  // --------------------------------------------------------------------------
  // Row keys
  // --------------------------------------------------------------------------

  // Each row of our data is an array of values rather than a dictionary. This
  // avoids some overhead from repeating the key string multiple times, and
  // speeds up the property accesses a bit. The following keys are well-known
  // indexes into the array for various properties.
  //
  // Note that the declaration order will also define the default display order.

  var BEGIN_KEY = 1;  // Start at 1 rather than 0 to simplify sorting code.
  var END_KEY = BEGIN_KEY;

  var KEY_COUNT = END_KEY++;
  var KEY_RUN_TIME = END_KEY++;
  var KEY_AVG_RUN_TIME = END_KEY++;
  var KEY_MAX_RUN_TIME = END_KEY++;
  var KEY_QUEUE_TIME = END_KEY++;
  var KEY_AVG_QUEUE_TIME = END_KEY++;
  var KEY_MAX_QUEUE_TIME = END_KEY++;
  var KEY_BIRTH_THREAD = END_KEY++;
  var KEY_DEATH_THREAD = END_KEY++;
  var KEY_PROCESS_TYPE = END_KEY++;
  var KEY_PROCESS_ID = END_KEY++;
  var KEY_FUNCTION_NAME = END_KEY++;
  var KEY_SOURCE_LOCATION = END_KEY++;
  var KEY_FILE_NAME = END_KEY++;
  var KEY_LINE_NUMBER = END_KEY++;

  var NUM_KEYS = END_KEY - BEGIN_KEY;

  // --------------------------------------------------------------------------
  // Aggregators
  // --------------------------------------------------------------------------

  // To generalize computing/displaying the aggregate "counts" for each column,
  // we specify an optional "Aggregator" class to use with each property.

  // The following are actually "Aggregator factories". They create an
  // aggregator instance by calling 'create()'. The instance is then fed
  // each row one at a time via the 'consume()' method. After all rows have
  // been consumed, the 'getValueAsText()' method will return the aggregated
  // value.

  /**
   * This aggregator counts the number of unique values that were fed to it.
   */
  var UniquifyAggregator = (function() {
    function Aggregator(key) {
      this.key_ = key;
      this.valuesSet_ = {};
    }

    Aggregator.prototype = {
      consume: function(e) {
        this.valuesSet_[e[this.key_]] = true;
      },

      getValueAsText: function() {
        return getDictionaryKeys(this.valuesSet_).length + ' unique'
      },
    };

    return {
      create: function(key) { return new Aggregator(key); }
    };
  })();

  /**
   * This aggregator sums a numeric field.
   */
  var SumAggregator = (function() {
    function Aggregator(key) {
      this.key_ = key;
      this.sum_ = 0;
    }

    Aggregator.prototype = {
      consume: function(e) {
        this.sum_ += e[this.key_];
      },

      getValueAsText: function() {
        return formatNumberAsText(this.sum_);
      },
    };

    return {
      create: function(key) { return new Aggregator(key); }
    };
  })();

  /**
   * This aggregator computes an average by summing two
   * numeric fields, and then dividing the totals.
   */
  var AvgAggregator = (function() {
    function Aggregator(numeratorKey, divisorKey) {
      this.numeratorKey_ = numeratorKey;
      this.divisorKey_ = divisorKey;

      this.numeratorSum_ = 0;
      this.divisorSum_ = 0;
    }

    Aggregator.prototype = {
      consume: function(e) {
        this.numeratorSum_ += e[this.numeratorKey_];
        this.divisorSum_ += e[this.divisorKey_];
      },

      getValueAsText: function() {
        return formatNumberAsText(this.numeratorSum_ / this.divisorSum_);
      },
    };

    return {
      create: function(numeratorKey, divisorKey) {
        return {
          create: function(key) {
            return new Aggregator(numeratorKey, divisorKey);
          },
        }
      }
    };
  })();

  /**
   * This aggregator finds the maximum for a numeric field.
   */
  var MaxAggregator = (function() {
    function Aggregator(key) {
      this.key_ = key;
      this.max_ = -Infinity;
    }

    Aggregator.prototype = {
      consume: function(e) {
        this.max_ = Math.max(this.max_, e[this.key_]);
      },

      getValueAsText: function() {
        return formatNumberAsText(this.max_);
      },
    };

    return {
      create: function(key) { return new Aggregator(key); }
    };
  })();

  // --------------------------------------------------------------------------
  // Key properties
  // --------------------------------------------------------------------------

  // Custom comparator for thread names (sorts main thread and IO thread
  // higher than would happen lexicographically.)
  var threadNameComparator =
      createLexicographicComparatorWithExceptions([
          'CrBrowserMain',
          'Chrome_IOThread',
          'Chrome_FileThread',
          'Chrome_HistoryThread',
          'Chrome_DBThread',
          'Still_Alive',
      ]);

  /**
   * Enumerates information about various keys. Such as whether their data is
   * expected to be numeric or is a string, a descriptive name (title) for the
   * property, and what function should be used to aggregate the property when
   * displayed in a column.
   *
   * --------------------------------------
   * The following properties are required:
   * --------------------------------------
   *
   *   [name]: This is displayed as the column's label.
   *   [aggregator]: Aggregator factory that is used to compute an aggregate
   *                 value for this column.
   *
   * --------------------------------------
   * The following properties are optional:
   * --------------------------------------
   *
   *   [inputJsonKey]: The corresponding key for this property in the original
   *                   JSON dictionary received from the browser. If this is
   *                   present, values for this key will be automatically
   *                   populated during import.
   *   [comparator]: A comparator function for sorting this column.
   *   [textPrinter]: A function that transforms values into the user-displayed
   *                  text shown in the UI. If unspecified, will default to the
   *                  "toString()" function.
   *   [cellAlignment]: The horizonal alignment to use for columns of this
   *                    property (for instance 'right'). If unspecified will
   *                    default to left alignment.
   *   [sortDescending]: When first clicking on this column, we will default to
   *                     sorting by |comparator| in ascending order. If this
   *                     property is true, we will reverse that to descending.
   */
  var KEY_PROPERTIES = [];

  KEY_PROPERTIES[KEY_PROCESS_ID] = {
    name: 'PID',
    cellAlignment: 'right',
    aggregator: UniquifyAggregator,
  };

  KEY_PROPERTIES[KEY_PROCESS_TYPE] = {
    name: 'Process type',
    aggregator: UniquifyAggregator,
  };

  KEY_PROPERTIES[KEY_BIRTH_THREAD] = {
    name: 'Birth thread',
    inputJsonKey: 'birth_thread',
    aggregator: UniquifyAggregator,
    comparator: threadNameComparator,
  };

  KEY_PROPERTIES[KEY_DEATH_THREAD] = {
    name: 'Exec thread',
    inputJsonKey: 'death_thread',
    aggregator: UniquifyAggregator,
    comparator: threadNameComparator,
  };

  KEY_PROPERTIES[KEY_FUNCTION_NAME] = {
    name: 'Function name',
    inputJsonKey: 'location.function_name',
    aggregator: UniquifyAggregator,
  };

  KEY_PROPERTIES[KEY_FILE_NAME] = {
    name: 'File name',
    inputJsonKey: 'location.file_name',
    aggregator: UniquifyAggregator,
  };

  KEY_PROPERTIES[KEY_LINE_NUMBER] = {
    name: 'Line number',
    cellAlignment: 'right',
    inputJsonKey: 'location.line_number',
    aggregator: UniquifyAggregator,
  };

  KEY_PROPERTIES[KEY_COUNT] = {
    name: 'Count',
    cellAlignment: 'right',
    sortDescending: true,
    textPrinter: formatNumberAsText,
    inputJsonKey: 'death_data.count',
    aggregator: SumAggregator,
  };

  KEY_PROPERTIES[KEY_QUEUE_TIME] = {
    name: 'Total queue time',
    cellAlignment: 'right',
    sortDescending: true,
    textPrinter: formatNumberAsText,
    inputJsonKey: 'death_data.queue_ms',
    aggregator: SumAggregator,
  };

  KEY_PROPERTIES[KEY_MAX_QUEUE_TIME] = {
    name: 'Max queue time',
    cellAlignment: 'right',
    sortDescending: true,
    textPrinter: formatNumberAsText,
    inputJsonKey: 'death_data.queue_ms_max',
    aggregator: MaxAggregator,
  };

  KEY_PROPERTIES[KEY_RUN_TIME] = {
    name: 'Total run time',
    cellAlignment: 'right',
    sortDescending: true,
    textPrinter: formatNumberAsText,
    inputJsonKey: 'death_data.run_ms',
    aggregator: SumAggregator,
  };

  KEY_PROPERTIES[KEY_AVG_RUN_TIME] = {
    name: 'Avg run time',
    cellAlignment: 'right',
    sortDescending: true,
    textPrinter: formatNumberAsText,
    aggregator: AvgAggregator.create(KEY_RUN_TIME, KEY_COUNT),
  };

  KEY_PROPERTIES[KEY_MAX_RUN_TIME] = {
    name: 'Max run time',
    cellAlignment: 'right',
    sortDescending: true,
    textPrinter: formatNumberAsText,
    inputJsonKey: 'death_data.run_ms_max',
    aggregator: MaxAggregator,
  };

  KEY_PROPERTIES[KEY_AVG_QUEUE_TIME] = {
    name: 'Avg queue time',
    cellAlignment: 'right',
    sortDescending: true,
    textPrinter: formatNumberAsText,
    aggregator: AvgAggregator.create(KEY_QUEUE_TIME, KEY_COUNT),
  };

  KEY_PROPERTIES[KEY_SOURCE_LOCATION] = {
    name: 'Source location',
    type: 'string',
    aggregator: UniquifyAggregator,
  };

  /**
   * Returns the string name for |key|.
   */
  function getNameForKey(key) {
    var props = KEY_PROPERTIES[key];
    if (props == undefined)
      throw 'Did not define properties for key: ' + key;
    return props.name;
  }

  /**
   * Ordered list of all keys. This is the order we generally want
   * to display the properties in. Default to declaration order.
   */
  var ALL_KEYS = [];
  for (var k = BEGIN_KEY; k < END_KEY; ++k)
    ALL_KEYS.push(k);

  // --------------------------------------------------------------------------
  // Default settings
  // --------------------------------------------------------------------------

  /**
   * List of keys for those properties which we want to initially omit
   * from the table. (They can be re-enabled by clicking [Edit columns]).
   */
  var INITIALLY_HIDDEN_KEYS = [
    KEY_FILE_NAME,
    KEY_LINE_NUMBER,
    KEY_QUEUE_TIME,
  ];

  /**
   * The ordered list of grouping choices to expose in the "Group by"
   * dropdowns. We don't include the numeric properties, since they
   * leads to awkward bucketing.
   */
  var GROUPING_DROPDOWN_CHOICES = [
    KEY_PROCESS_TYPE,
    KEY_PROCESS_ID,
    KEY_BIRTH_THREAD,
    KEY_DEATH_THREAD,
    KEY_FUNCTION_NAME,
    KEY_SOURCE_LOCATION,
    KEY_FILE_NAME,
    KEY_LINE_NUMBER,
  ];

  /**
   * The ordered list of sorting choices to expose in the "Sort by"
   * dropdowns.
   */
  var SORT_DROPDOWN_CHOICES = ALL_KEYS;

  /**
   * The ordered list of all columns that can be displayed in the tables (not
   * including whatever has been hidden via [Edit Columns]).
   */
  var ALL_TABLE_COLUMNS = ALL_KEYS;

  /**
   * The initial keys to sort by when loading the page (can be changed later).
   */
  var INITIAL_SORT_KEYS = [-KEY_COUNT];

  /**
   * The default sort keys to use when nothing has been specified.
   */
  var DEFAULT_SORT_KEYS = [-KEY_COUNT];

  /**
   * The initial keys to group by when loading the page (can be changed later).
   */
  var INITIAL_GROUP_KEYS = [];

  // --------------------------------------------------------------------------
  // General utility functions
  // --------------------------------------------------------------------------

  /**
   * Returns a list of all the keys in |dict|.
   */
  function getDictionaryKeys(dict) {
    var keys = [];
    for (var key in dict) {
      keys.push(key);
    }
    return keys;
  }

  /**
   * Formats the number |x| as a decimal integer. Strips off any decimal parts,
   * and comma separates the number every 3 characters.
   */
  function formatNumberAsText(x) {
    var orig = x.toFixed(0);

    var parts = [];
    for (var end = orig.length; end > 0; ) {
      var chunk = Math.min(end, 3);
      parts.push(orig.substr(end-chunk, chunk));
      end -= chunk;
    }
    return parts.reverse().join(',');
  }

  /**
   * Simple comparator function which works for both strings and numbers.
   */
  function simpleCompare(a, b) {
    if (a == b)
      return 0;
    if (a < b)
      return -1;
    return 1;
  }

  /**
   * Returns a comparator function that compares values lexicographically,
   * but special-cases the values in |orderedList| to have a higher
   * rank.
   */
  function createLexicographicComparatorWithExceptions(orderedList) {
    var valueToRankMap = {};
    for (var i = 0; i < orderedList.length; ++i)
      valueToRankMap[orderedList[i]] = i;

    function getCustomRank(x) {
      var rank = valueToRankMap[x];
      if (rank == undefined)
        rank = Infinity;  // Unmatched.
      return rank;
    }

    return function(a, b) {
      var aRank = getCustomRank(a);
      var bRank = getCustomRank(b);

      // Not matched by any of our exceptions.
      if (aRank == bRank)
        return simpleCompare(a, b);

      if (aRank < bRank)
        return -1;
      return 1;
    };
  }

  /**
   * Returns dict[key]. Note that if |key| contains periods (.), they will be
   * interpreted as meaning a sub-property.
   */
  function getPropertyByPath(dict, key) {
    var cur = dict;
    var parts = key.split('.');
    for (var i = 0; i < parts.length; ++i) {
      if (cur == undefined)
        return undefined;
      cur = cur[parts[i]];
    }
    return cur;
  }

  /**
   * Creates and appends a DOM node of type |tagName| to |parent|. Optionally,
   * sets the new node's text to |opt_text|. Returns the newly created node.
   */
  function addNode(parent, tagName, opt_text) {
    var n = parent.ownerDocument.createElement(tagName);
    parent.appendChild(n);
    if (opt_text != undefined) {
      addText(n, opt_text);
    }
    return n;
  }

  /**
   * Adds |text| to |parent|.
   */
  function addText(parent, text) {
    var textNode = parent.ownerDocument.createTextNode(text);
    parent.appendChild(textNode);
    return textNode;
  }

  /**
   * Deletes all the strings in |array| which have a key in |valueSet|.
   */
  function deleteStringsFromArrayMatching(array, valueSet) {
    for (var i = 0; i < array.length; ) {
      if (valueSet[array[i]]) {
        array.splice(i, 1);
      } else {
        i++;
      }
    }
  }

  /**
   * Deletes all the repeated ocurrences of strings in |array|.
   */
  function deleteDuplicateStringsFromArray(array) {
    // Build up set of each entry in array.
    var seenSoFar = {};

    for (var i = 0; i < array.length; ) {
      var value = array[i];
      if (seenSoFar[value]) {
        array.splice(i, 1);
      } else {
        seenSoFar[value] = true;
        i++;
      }
    }
  }

  function trimWhitespace(text) {
    var m = /^\s*(.*)\s*$/.exec(text);
    return m[1];
  }

  /**
   * Selects the option in |select| which has a value of |value|.
   */
  function setSelectedOptionByValue(select, value) {
    for (var i = 0; i < select.options.length; ++i) {
      if (select.options[i].value == value) {
        select.options[i].selected = true;
        return true;
      }
    }
    return false;
  }

  // --------------------------------------------------------------------------
  // Functions that augment, bucket, and compute aggregates for the input data.
  // --------------------------------------------------------------------------

  /**
   * Selects all the data in |rows| which are matched by |filterFunc|, and
   * buckets the results using |entryToGroupKeyFunc|. For each bucket aggregates
   * are computed, and the results are sorted.
   *
   * Returns a dictionary whose keys are the group name, and the value is an
   * objected containing two properties: |rows| and |aggregates|.
   */
  function prepareData(rows, entryToGroupKeyFunc, filterFunc, sortingFunc) {
    var groupedData = {};

    for (var i = 0; i < rows.length; ++i) {
      var e = rows[i];

      if (!filterFunc(e))
        continue;  // Not matched by our filter, discard the row.

      var groupKey = entryToGroupKeyFunc(e);

      var groupData = groupedData[groupKey];
      if (!groupData) {
        groupData = {
          aggregates: initializeAggregates(ALL_KEYS),
          rows: [],
        };
        groupedData[groupKey] = groupData;
      }

      // Add the row to our list.
      groupData.rows.push(e);

      // Update aggregates for each column.
      for (var key in groupData.aggregates)
        groupData.aggregates[key].consume(e);
    }

    // Sort all the data.
    for (var groupKey in groupedData)
      groupedData[groupKey].rows.sort(sortingFunc);

    return groupedData;
  }

  /**
   * Adds new derived properties to row. Mutates the provided dictionary |e|.
   */
  function augmentDataRow(e) {
    e[KEY_AVG_QUEUE_TIME] = e[KEY_QUEUE_TIME] / e[KEY_COUNT];
    e[KEY_AVG_RUN_TIME] = e[KEY_RUN_TIME] / e[KEY_COUNT];
    e[KEY_SOURCE_LOCATION] = e[KEY_FILE_NAME] + ' [' + e[KEY_LINE_NUMBER] + ']';
  }

  /**
   * Creates and initializes an aggregator object for each key in |columns|.
   * Returns a dictionary whose keys are values from |columns|, and whose
   * values are Aggregator instances.
   */
  function initializeAggregates(columns) {
    var aggregates = {};

    for (var i = 0; i < columns.length; ++i) {
      var key = columns[i];
      var aggregatorFactory = KEY_PROPERTIES[key].aggregator;
      if (aggregatorFactory)
        aggregates[key] = aggregatorFactory.create(key);
    }

    return aggregates;
  }

  // --------------------------------------------------------------------------
  // HTML drawing code
  // --------------------------------------------------------------------------

  /**
   * Draws a title into |parent| that describes |groupKey|.
   */
  function drawGroupTitle(parent, groupKey) {
    if (groupKey.length  == 0) {
      // Empty group key means there was no grouping.
      return;
    }

    var parent = addNode(parent, 'div');
    parent.className = 'group-title-container';

    // Each component of the group key represents the "key=value" constraint for
    // this group. Show these as an AND separated list.
    for (var i = 0; i < groupKey.length; ++i) {
      if (i > 0)
        addNode(parent, 'i', ' and ');
      var e = groupKey[i];
      addNode(parent, 'b', getNameForKey(e.key) + ' = ');
      addNode(parent, 'span', e.value);
    }
  }

  /**
   * Renders the information for a particular group.
   */
  function drawGroup(parent, groupKey, groupData, columns,
                     columnOnClickHandler, currentSortKeys) {
    var div = addNode(parent, 'div');
    div.className = 'group-container';

    drawGroupTitle(div, groupKey);

    var table = addNode(div, 'table');

    drawDataTable(table, groupData, columns, columnOnClickHandler,
                  currentSortKeys);
  }

  /**
   * Renders a row that describes all the aggregate values for |columns|.
   */
  function drawAggregateRow(tbody, aggregates, columns) {
    var tr = addNode(tbody, 'tr');
    tr.className = 'aggregator-row';

    for (var i = 0; i < columns.length; ++i) {
      var key = columns[i];
      var td = addNode(tr, 'td');

      // Most of our outputs are numeric, so we want to align them to the right.
      // However for the  unique counts we will center.
      if (KEY_PROPERTIES[key].aggregator == UniquifyAggregator) {
        td.align = 'center';
      } else {
        td.align = 'right';
      }

      var aggregator = aggregates[key];
      if (aggregator)
        td.innerText = aggregator.getValueAsText();
    }
  }

  /**
   * Renders a table which summarizes all |column| fields for |data|.
   */
  function drawDataTable(table, data, columns, columnOnClickHandler,
                         currentSortKeys) {
    table.className = 'results-table';
    var thead = addNode(table, 'thead');
    var tbody = addNode(table, 'tbody');

    drawAggregateRow(thead, data.aggregates, columns);
    drawTableHeader(thead, columns, columnOnClickHandler, currentSortKeys);
    drawTableBody(tbody, data.rows, columns);
  }

  function drawTableHeader(thead, columns, columnOnClickHandler,
                           currentSortKeys) {
    var tr = addNode(thead, 'tr');
    for (var i = 0; i < columns.length; ++i) {
      var key = columns[i];
      var th = addNode(tr, 'th', getNameForKey(key));
      th.onclick = columnOnClickHandler.bind(this, key);

      // Draw an indicator if we are currently sorted on this column.
      // TODO(eroman): Should use an icon instead of asterisk!
      for (var j = 0; j < currentSortKeys.length; ++j) {
        if (sortKeysMatch(currentSortKeys[j], key)) {
          var sortIndicator = addNode(th, 'span', '*');
          sortIndicator.style.color = 'red';
          if (sortKeyIsReversed(currentSortKeys[j])) {
            // Use double-asterisk for descending columns.
            addText(sortIndicator, '*');
          }
          break;
        }
      }
    }
  }

  function getTextValueForProperty(key, value) {
    var textPrinter = KEY_PROPERTIES[key].textPrinter;
    if (textPrinter)
      return textPrinter(value);
    return value.toString();
  }

  /**
   * Renders the property value |value| into cell |td|. The name of this
   * property is |key|.
   */
  function drawValueToCell(td, key, value) {
    // Get a text representation of the value.
    var text = getTextValueForProperty(key, value);

    // Apply the desired cell alignment.
    var cellAlignment = KEY_PROPERTIES[key].cellAlignment;
    if (cellAlignment)
      td.align = cellAlignment;

    // String values can get pretty long. If the string contains no spaces, then
    // CSS fails to wrap it, and it overflows the cell causing the table to get
    // really big. We solve this using a hack: insert a <wbr> element after
    // every single character. This will allow the rendering engine to wrap the
    // value, and hence avoid it overflowing!
    var kMinLengthBeforeWrap = 20;

    addText(td, text.substr(0, kMinLengthBeforeWrap));
    for (var i = kMinLengthBeforeWrap; i < text.length; ++i) {
      addNode(td, 'wbr');
      addText(td, text.substr(i, 1));
    }
  }

  function drawTableBody(tbody, rows, columns) {
    for (var i = 0; i < rows.length; ++i) {
      var e = rows[i];

      var tr = addNode(tbody, 'tr');

      for (var c = 0; c < columns.length; ++c) {
        var key = columns[c];
        var value = e[key];

        var td = addNode(tr, 'td');
        drawValueToCell(td, key, value);
      }
    }
  }

  // --------------------------------------------------------------------------
  // Helper code for handling the sort and grouping dropdowns.
  // --------------------------------------------------------------------------

  function addOptionsForGroupingSelect(select) {
    // Add "no group" choice.
    addNode(select, 'option', '---').value = '';

    for (var i = 0; i < GROUPING_DROPDOWN_CHOICES.length; ++i) {
      var key = GROUPING_DROPDOWN_CHOICES[i];
      var option = addNode(select, 'option', getNameForKey(key));
      option.value = key;
    }
  }

  function addOptionsForSortingSelect(select) {
    // Add "no sort" choice.
    addNode(select, 'option', '---').value = '';

    // Add a divider.
    addNode(select, 'optgroup').label = '';

    for (var i = 0; i < SORT_DROPDOWN_CHOICES.length; ++i) {
      var key = SORT_DROPDOWN_CHOICES[i];
      addNode(select, 'option', getNameForKey(key)).value = key;
    }

    // Add a divider.
    addNode(select, 'optgroup').label = '';

    // Add the same options, but for descending.
    for (var i = 0; i < SORT_DROPDOWN_CHOICES.length; ++i) {
      var key = SORT_DROPDOWN_CHOICES[i];
      var n = addNode(select, 'option', getNameForKey(key) + ' (DESC)');
      n.value = reverseSortKey(key);
    }
  }

  /**
   * Helper function used to update the sorting and grouping lists after a
   * dropdown changes.
   */
  function updateKeyListFromDropdown(list, i, select) {
    // Update the list.
    if (i < list.length) {
      list[i] = select.value;
    } else {
      list.push(select.value);
    }

    // Normalize the list, so setting 'none' as primary zeros out everything
    // else.
    for (var i = 0; i < list.length; ++i) {
      if (list[i] == '') {
        list.splice(i, list.length - i);
        break;
      }
    }
  }

  /**
   * Comparator for property |key|, having values |value1| and |value2|.
   * If the key has defined a custom comparator use it. Otherwise use a
   * default "less than" comparison.
   */
  function compareValuesForKey(key, value1, value2) {
    var comparator = KEY_PROPERTIES[key].comparator;
    if (comparator)
      return comparator(value1, value2);
    return simpleCompare(value1, value2);
  }

  function reverseSortKey(key) {
    return -key;
  }

  function sortKeyIsReversed(key) {
    return key < 0;
  }

  function sortKeysMatch(key1, key2) {
    return Math.abs(key1) == Math.abs(key2);
  }

  // --------------------------------------------------------------------------

  /**
   * @constructor
   */
  function MainView() {
    // Make sure we have a definition for each key.
    for (var k = BEGIN_KEY; k < END_KEY; ++k) {
      if (!KEY_PROPERTIES[k])
        throw 'KEY_PROPERTIES[] not defined for key: ' + k;
    }

    this.init_();
  }

  MainView.prototype = {
    addData: function(data) {
      var pid = data.process_id;
      var ptype = data.process_type;

      // Augment each data row with the process information.
      var rows = data.list;
      for (var i = 0; i < rows.length; ++i) {
        // Transform the data from a dictionary to an array. This internal
        // representation is more compact and faster to access.
        var origRow = rows[i];
        var newRow = [];

        newRow[KEY_PROCESS_ID] = pid;
        newRow[KEY_PROCESS_TYPE] = ptype;

        // Copy over the known properties which have a 1:1 mapping with JSON.
        for (var k = BEGIN_KEY; k < END_KEY; ++k) {
          var inputJsonKey = KEY_PROPERTIES[k].inputJsonKey;
          if (inputJsonKey != undefined) {
            newRow[k] = getPropertyByPath(origRow, inputJsonKey);
          }
        }

        // Add our computed properties.
        augmentDataRow(newRow);

        this.allData_.push(newRow);
      }

      this.redrawData_();
    },

    redrawData_: function() {
      var data = this.allData_;

      // Figure out what columns to include, based on the selected checkboxes.
      var columns = [];
      for (var i = 0; i < ALL_TABLE_COLUMNS.length; ++i) {
        var key = ALL_TABLE_COLUMNS[i];
        if (this.selectionCheckboxes_[key].checked) {
          columns.push(key);
        }
      }

      // Group, aggregate, filter, and sort the data.
      var groupedData = prepareData(
          data, this.getGroupingFunction_(), this.getFilterFunction_(),
          this.getSortingFunction_());

      // Figure out a display order for the groups.
      var groupKeys = getDictionaryKeys(groupedData);
      groupKeys.sort(this.getGroupSortingFunction_());

      // Clear the results div, sine we may be overwriting older data.
      var parent = $(RESULTS_DIV_ID);
      parent.innerHTML = '';

      if (groupKeys.length > 0) {
        // The grouping will be the the same for each so just pick the first.
        var randomGroupKey = JSON.parse(groupKeys[0]);

        // The grouped properties are going to be the same for each row in our,
        // table, so avoid drawing them in our table!
        var keysToExcludeSet = {};

        for (var i = 0; i < randomGroupKey.length; ++i)
          keysToExcludeSet[randomGroupKey[i].key] = true;
        columns = columns.slice(0);
        deleteStringsFromArrayMatching(columns, keysToExcludeSet);
      }

      var columnOnClickHandler = this.onClickColumn_.bind(this);

      // Draw each group.
      for (var i = 0; i < groupKeys.length; ++i) {
        var groupKeyString = groupKeys[i];
        var groupData = groupedData[groupKeyString];
        var groupKey = JSON.parse(groupKeyString);

        drawGroup(parent, groupKey, groupData, columns,
                  columnOnClickHandler, this.currentSortKeys_);
      }
    },

    init_: function() {
      this.allData_ = [];
      this.fillSelectionCheckboxes_($(COLUMN_TOGGLES_CONTAINER_ID));

      $(FILTER_SEARCH_ID).onsearch = this.onChangedFilter_.bind(this);

      this.currentSortKeys_ = INITIAL_SORT_KEYS.slice(0);
      this.currentGroupingKeys_ = INITIAL_GROUP_KEYS.slice(0);

      this.fillGroupingDropdowns_();
      this.fillSortingDropdowns_();

      $(EDIT_COLUMNS_LINK_ID).onclick = this.toggleEditColumns_.bind(this);
    },

    toggleEditColumns_: function() {
      var n = $(EDIT_COLUMNS_ROW);
      if (n.style.display == '') {
        n.style.display = 'none';
      } else {
        n.style.display = '';
      }
    },

    fillSelectionCheckboxes_: function(parent) {
      this.selectionCheckboxes_ = {};

      for (var i = 0; i < ALL_TABLE_COLUMNS.length; ++i) {
        var key = ALL_TABLE_COLUMNS[i];
        var checkbox = addNode(parent, 'input');
        checkbox.type = 'checkbox';
        checkbox.onchange = this.onSelectCheckboxChanged_.bind(this);
        checkbox.checked = true;
        addNode(parent, 'span', getNameForKey(key) + ' ');
        this.selectionCheckboxes_[key] = checkbox;
      }

      for (var i = 0; i < INITIALLY_HIDDEN_KEYS.length; ++i) {
        this.selectionCheckboxes_[INITIALLY_HIDDEN_KEYS[i]].checked = false;
      }
    },

    fillGroupingDropdowns_: function() {
      var parent = $(GROUP_BY_CONTAINER_ID);
      parent.innerHTML = '';

      for (var i = 0; i <= this.currentGroupingKeys_.length; ++i) {
        // Add a dropdown.
        var select = addNode(parent, 'select');
        select.onchange = this.onChangedGrouping_.bind(this, select, i);

        addOptionsForGroupingSelect(select);

        if (i < this.currentGroupingKeys_.length) {
          var key = this.currentGroupingKeys_[i];
          setSelectedOptionByValue(select, key);
        }
      }
    },

    fillSortingDropdowns_: function() {
      var parent = $(SORT_BY_CONTAINER_ID);
      parent.innerHTML = '';

      for (var i = 0; i <= this.currentSortKeys_.length; ++i) {
        // Add a dropdown.
        var select = addNode(parent, 'select');
        select.onchange = this.onChangedSorting_.bind(this, select, i);

        addOptionsForSortingSelect(select);

        if (i < this.currentSortKeys_.length) {
          var key = this.currentSortKeys_[i];
          setSelectedOptionByValue(select, key);
        }
      }
    },

    onChangedGrouping_: function(select, i) {
      updateKeyListFromDropdown(this.currentGroupingKeys_, i, select);
      this.fillGroupingDropdowns_();
      this.redrawData_();
    },

    onChangedSorting_: function(select, i) {
      updateKeyListFromDropdown(this.currentSortKeys_, i, select);
      this.fillSortingDropdowns_();
      this.redrawData_();
    },

    onSelectCheckboxChanged_: function() {
      this.redrawData_();
    },

    onChangedFilter_: function() {
      this.redrawData_();
    },

    /**
     * When left-clicking a column, change the primary sort order to that
     * column. If we were already sorted on that column then reverse the order.
     *
     * When alt-clicking, add a secondary sort column. Similarly, if
     * alt-clicking a column which was already being sorted on, reverse its
     * order.
     */
    onClickColumn_: function(key, event) {
      // If this property wants to start off in descending order rather then
      // ascending, flip it.
      if (KEY_PROPERTIES[key].sortDescending)
        key = reverseSortKey(key);

      // Scan through our sort order and see if we are already sorted on this
      // key. If so, reverse that sort ordering.
      var found_i = -1;
      for (var i = 0; i < this.currentSortKeys_.length; ++i) {
        var curKey = this.currentSortKeys_[i];
        if (sortKeysMatch(curKey, key)) {
          this.currentSortKeys_[i] = reverseSortKey(curKey);
          found_i = i;
          break;
        }
      }

      if (event.altKey) {
        if (found_i == -1) {
          // If we weren't already sorted on the column that was alt-clicked,
          // then add it to our sort.
          this.currentSortKeys_.push(key);
        }
      } else {
        if (found_i != 0 ||
            !sortKeysMatch(this.currentSortKeys_[found_i], key)) {
          // If the column we left-clicked wasn't already our primary column,
          // make it so.
          this.currentSortKeys_ = [key];
        } else {
          // If the column we left-clicked was already our primary column (and
          // we just reversed it), remove any secondary sorts.
          this.currentSortKeys_.length = 1;
        }
      }

      this.fillSortingDropdowns_();
      this.redrawData_();
    },

    getSortingFunction_: function() {
      var sortKeys = this.currentSortKeys_.slice(0);

      // Eliminate the empty string keys (which means they were unspecified).
      deleteStringsFromArrayMatching(sortKeys, {'': true});

      // If no sort is specified, use our default sort.
      if (sortKeys.length == 0)
        sortKeys = [DEFAULT_SORT_KEYS];

      return function(a, b) {
        for (var i = 0; i < sortKeys.length; ++i) {
          var key = Math.abs(sortKeys[i]);
          var factor = sortKeys[i] < 0 ? -1 : 1;

          var propA = a[key];
          var propB = b[key];

          var comparison = compareValuesForKey(key, propA, propB);
          comparison *= factor;  // Possibly reverse the ordering.

          if (comparison != 0)
            return comparison;
        }

        // Tie breaker.
        return simpleCompare(JSON.stringify(a), JSON.stringify(b));
      };
    },

    getGroupSortingFunction_: function() {
      return function(a, b) {
        var groupKey1 = JSON.parse(a);
        var groupKey2 = JSON.parse(b);

        for (var i = 0; i < groupKey1.length; ++i) {
          var comparison = compareValuesForKey(
              groupKey1[i].key,
              groupKey1[i].value,
              groupKey2[i].value);

          if (comparison != 0)
            return comparison;
        }

        // Tie breaker.
        return simpleCompare(a, b);
      };
    },

    getFilterFunction_: function() {
      var searchStr = $(FILTER_SEARCH_ID).value;

      // Normalize the search expression.
      searchStr = trimWhitespace(searchStr);
      searchStr = searchStr.toLowerCase();

      return function(x) {
        // Match everything when there was no filter.
        if (searchStr == '')
          return true;

        // Treat the search text as a LOWERCASE substring search.
        for (var k = BEGIN_KEY; k < END_KEY; ++k) {
          var propertyText = getTextValueForProperty(k, x[k]);
          if (propertyText.toLowerCase().indexOf(searchStr) != -1)
            return true;
        }

        return false;
      };
    },

    getGroupingFunction_: function() {
      var groupings = this.currentGroupingKeys_.slice(0);

      // Eliminate the empty string groupings (which means they were
      // unspecified).
      deleteStringsFromArrayMatching(groupings, {'': true});

      // Eliminate duplicate primary/secondary group by directives, since they
      // are redundant.
      deleteDuplicateStringsFromArray(groupings);

      return function(e) {
        var groupKey = [];

        for (var i = 0; i < groupings.length; ++i) {
          var entry = {key: groupings[i],
                       value: e[groupings[i]]};
          groupKey.push(entry);
        }

        return JSON.stringify(groupKey);
      };
    },
  };

  return MainView;
})();
