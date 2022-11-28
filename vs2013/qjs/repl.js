// THIS_SOURCES_HAS_BEEN_TRANSLATED 
/*  *QuickJS读取评估打印循环**版权所有(C)2017-2019 Fabrice Bellard*版权所有(C)2017-2019查理·戈登**现向任何获取复制品的人免费授予许可*本软件及相关文档文件(本软件)，以处理*在软件中不受限制，包括但不限于*使用、复制、修改、合并、发布、分发、再许可和/或销售*软件的副本，并允许软件的接受者*为此而配备的，须符合以下条件：**上述版权声明和本许可声明应包括在*本软件的所有副本或主要部分。**软件按原样提供，不提供任何形式的担保，明示或*默示，包括但不限于适销性保证，*适用于某一特定目的和不侵权。在任何情况下都不应*作者或版权所有者对任何索赔、损害或其他*法律责任，无论是在合同诉讼、侵权诉讼或其他诉讼中，*出于或与软件有关，或与软件的使用或其他交易有关*软件。 */ 
//  “使用条带”；

import * as std from "std";
import * as os from "os";

(function(g) {
    /*  添加‘os’和‘std’绑定。 */ 
    g.os = os;
    g.std = std;
    
    /*  关闭全局对象。 */ 
    var Object = g.Object;
    var String = g.String;
    var Array = g.Array;
    var Date = g.Date;
    var Math = g.Math;
    var isFinite = g.isFinite;
    var parseFloat = g.parseFloat;

    /*  XXX：使用预处理器？ */ 
    var config_numcalc = (typeof os.open === "undefined");
    var has_jscalc = (typeof Fraction === "function");
    var has_bignum = (typeof BigFloat === "function");
    
    var colors = {
        none:    "\x1b[0m",
        black:   "\x1b[30m",
        red:     "\x1b[31m",
        green:   "\x1b[32m",
        yellow:  "\x1b[33m",
        blue:    "\x1b[34m",
        magenta: "\x1b[35m",
        cyan:    "\x1b[36m",
        white:   "\x1b[37m",
        gray:    "\x1b[30;1m",
        grey:    "\x1b[30;1m",
        bright_red:     "\x1b[31;1m",
        bright_green:   "\x1b[32;1m",
        bright_yellow:  "\x1b[33;1m",
        bright_blue:    "\x1b[34;1m",
        bright_magenta: "\x1b[35;1m",
        bright_cyan:    "\x1b[36;1m",
        bright_white:   "\x1b[37;1m",
    };

    var styles;
    if (config_numcalc) {
        styles = {
            'default':    'black',
            'comment':    'white',
            'string':     'green',
            'regex':      'cyan',
            'number':     'green',
            'keyword':    'blue',
            'function':   'gray',
            'type':       'bright_magenta',
            'identifier': 'yellow',
            'error':      'bright_red',
            'result':     'black',
            'error_msg':  'bright_red',
        };
    } else {
        styles = {
            'default':    'bright_green',
            'comment':    'white',
            'string':     'bright_cyan',
            'regex':      'cyan',
            'number':     'green',
            'keyword':    'bright_white',
            'function':   'bright_yellow',
            'type':       'bright_magenta',
            'identifier': 'bright_green',
            'error':      'red',
            'result':     'bright_white',
            'error_msg':  'bright_red',
        };
    }

    var history = [];
    var clip_board = "";
    var prec;
    var expBits;
    var log2_10;
    
    var pstate = "";
    var prompt = "";
    var plen = 0;
    var ps1;
    if (config_numcalc)
        ps1 = "> ";
    else
        ps1 = "qjs > ";
    var ps2 = "  ... ";
    var utf8 = true;
    var show_time = false;
    var show_colors = true;
    var eval_time = 0;
    
    var mexpr = "";
    var level = 0;
    var cmd = "";
    var cursor_pos = 0;
    var last_cmd = "";
    var last_cursor_pos = 0;
    var history_index;
    var this_fun, last_fun;
    var quote_flag = false;

    var utf8_state = 0;
    var utf8_val = 0;

    var term_fd;
    var term_read_buf;
    var term_width;
    /*  光标在终端中的当前X位置。 */ 
    var term_cursor_x = 0; 
    
    function termInit() {
        var tab;
        term_fd = std.in.fileno();
        
        /*  获取终端大小。 */ 
        term_width = 80;
        if (os.isatty(term_fd)) {
            if (os.ttyGetWinSize) {
                tab = os.ttyGetWinSize(term_fd);
                if (tab)
                    term_width = tab[0];
            }
            if (os.ttySetRaw) {
                /*  将TTY设置为RAW模式。 */ 
                os.ttySetRaw(term_fd);
            }
        }

        /*  安装Ctrl-C信号处理程序。 */ 
        os.signal(os.SIGINT, sigint_handler);

        /*  安装处理程序以读取标准输入。 */ 
        term_read_buf = new Uint8Array(64);
        os.setReadHandler(term_fd, term_read_handler);
    }

    function sigint_handler() {
        /*  将Ctrl-C发送到Readline。 */ 
        handle_byte(3);
    }
    
    function term_read_handler() {
        var l, i;
        l = os.read(term_fd, term_read_buf.buffer, 0, term_read_buf.length);
        for(i = 0; i < l; i++)
            handle_byte(term_read_buf[i]);
    }
    
    function handle_byte(c) {
        if (!utf8) {
            handle_char(c);
        } else if (utf8_state !== 0 && (c >= 0x80 && c < 0xc0)) {
            utf8_val = (utf8_val << 6) | (c & 0x3F);
            utf8_state--;
            if (utf8_state === 0) {
                handle_char(utf8_val);
            }
        } else if (c >= 0xc0 && c < 0xf8) {
            utf8_state = 1 + (c >= 0xe0) + (c >= 0xf0);
            utf8_val = c & ((1 << (6 - utf8_state)) - 1);
        } else {
            utf8_state = 0;
            handle_char(c);
        }
    }
    
    function is_alpha(c) {
        return typeof c === "string" &&
            ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
    }
    
    function is_digit(c) {
        return typeof c === "string" && (c >= '0' && c <= '9');
    }

    function is_word(c) {
        return typeof c === "string" &&
            (is_alpha(c) || is_digit(c) || c == '_' || c == '$');
    }

    function is_balanced(a, b) {
        switch (a + b) {
        case "()":
        case "[]":
        case "{}":
            return true;
        }
        return false;
    }

    function print_color_text(str, start, style_names) {
        var i, j;
        for (j = start; j < str.length;) {
            var style = style_names[i = j];
            while (++j < str.length && style_names[j] == style)
                continue;
            std.puts(colors[styles[style] || 'default']);
            std.puts(str.substring(i, j));
            std.puts(colors['none']);
        }
    }

    function print_csi(n, code) {
        std.puts("\x1b[" + ((n != 1) ? n : "") + code);
    }

    function move_cursor(delta) {
        var i, l;
        if (delta > 0) {
            while (delta != 0) {
                if (term_cursor_x == (term_width - 1)) {
                    std.puts("\n"); /* translated to CRLF */
                    term_cursor_x = 0;
                    delta--;
                } else {
                    l = Math.min(term_width - 1 - term_cursor_x, delta);
                    print_csi(l, "C"); /* right */
                    delta -= l; 
                    term_cursor_x += l;
                }
            }
        } else {
            delta = -delta;
            while (delta != 0) {
                if (term_cursor_x == 0) {
                    print_csi(1, "A"); /* up */
                    print_csi(term_width - 1, "C"); /* right */
                    delta--;
                    term_cursor_x = term_width - 1;
                } else {
                    l = Math.min(delta, term_cursor_x);
                    print_csi(l, "D"); /* left */
                    delta -= l;
                    term_cursor_x -= l;
                }
            }
        }
    }

    function update() {
        var i;

        if (cmd != last_cmd) {
            if (!show_colors && last_cmd.substring(0, last_cursor_pos) == cmd.substring(0, last_cursor_pos)) {
                /*  优化常见案例。 */ 
                std.puts(cmd.substring(last_cursor_pos));
            } else {
                /*  转到行的开头。 */ 
                move_cursor(-last_cursor_pos);
                if (show_colors) {
                    var str = mexpr ? mexpr + '\n' + cmd : cmd;
                    var start = str.length - cmd.length;
                    var colorstate = colorize_js(str);
                    print_color_text(str, start, colorstate[2]);
                } else {
                    std.puts(cmd);
                }
            }
            /*  注意：假设没有代理项对。 */ 
            term_cursor_x = (term_cursor_x + cmd.length) % term_width;
            if (term_cursor_x == 0) {
                /*  将光标显示在下一行。 */ 
                std.puts(" \x08");
            }
            /*  删除尾随字符。 */ 
            std.puts("\x1b[J");
            last_cmd = cmd;
            last_cursor_pos = cmd.length;
        }
        move_cursor(cursor_pos - last_cursor_pos);
        last_cursor_pos = cursor_pos;
        std.out.flush();
    }

    /*  编辑命令。 */ 
    function insert(str) {
        if (str) {
            cmd = cmd.substring(0, cursor_pos) + str + cmd.substring(cursor_pos);
            cursor_pos += str.length;
        }
    }

    function quoted_insert() {
        quote_flag = true;
    }

    function abort() {
        cmd = "";
        cursor_pos = 0;
        return -2;
    }

    function alert() {
    }

    function beginning_of_line() {
        cursor_pos = 0;
    }

    function end_of_line() {
        cursor_pos = cmd.length;
    }

    function forward_char() {
        if (cursor_pos < cmd.length)
            cursor_pos++;
    }

    function backward_char() {
        if (cursor_pos > 0)
            cursor_pos--;
    }

    function skip_word_forward(pos) {
        while (pos < cmd.length && !is_word(cmd.charAt(pos)))
            pos++;
        while (pos < cmd.length && is_word(cmd.charAt(pos)))
            pos++;
        return pos;
    }

    function skip_word_backward(pos) {
        while (pos > 0 && !is_word(cmd.charAt(pos - 1)))
            pos--;
        while (pos > 0 && is_word(cmd.charAt(pos - 1)))
            pos--;
        return pos;
    }

    function forward_word() {
        cursor_pos = skip_word_forward(cursor_pos);
    }

    function backward_word() {
        cursor_pos = skip_word_backward(cursor_pos);
    }        

    function accept_line() {
        std.puts("\n");
        history_add(cmd);
        return -1;
    }

    function history_add(str) {
        if (str) {
            history.push(str);
        }
        history_index = history.length;
    }

    function previous_history() {
        if (history_index > 0) {
            if (history_index == history.length) {
                history.push(cmd);
            }
            history_index--;
            cmd = history[history_index];
            cursor_pos = cmd.length;
        }
    }

    function next_history() {
        if (history_index < history.length - 1) {
            history_index++;
            cmd = history[history_index];
            cursor_pos = cmd.length;
        }
    }

    function history_search(dir) {
        var pos = cursor_pos;
        for (var i = 1; i <= history.length; i++) {
            var index = (history.length + i * dir + history_index) % history.length;
            if (history[index].substring(0, pos) == cmd.substring(0, pos)) {
                history_index = index;
                cmd = history[index];
                return;
            }
        }
    }

    function history_search_backward() {
        return history_search(-1);
    }

    function history_search_forward() {
        return history_search(1);
    }

    function delete_char_dir(dir) {
        var start = cursor_pos - (dir < 0);
        var end = start + 1;
        if (start >= 0 && start < cmd.length) {
            if (last_fun === kill_region) {
                kill_region(start, end, dir);
            } else {
                cmd = cmd.substring(0, start) + cmd.substring(end);
                cursor_pos = start;
            }
        }
    }

    function delete_char() {
        delete_char_dir(1);
    }

    function control_d() {
        if (cmd.length == 0) {
            std.puts("\n");
            return -3; /*  退出读取求值打印循环。 */ 
        } else {
            delete_char_dir(1);
        }
    }

    function backward_delete_char() {
        delete_char_dir(-1);
    }

    function transpose_chars() {
        var pos = cursor_pos;
        if (cmd.length > 1 && pos > 0) {
            if (pos == cmd.length)
                pos--;
            cmd = cmd.substring(0, pos - 1) + cmd.substring(pos, pos + 1) +
                cmd.substring(pos - 1, pos) + cmd.substring(pos + 1);
            cursor_pos = pos + 1;
        }
    }

    function transpose_words() {
        var p1 = skip_word_backward(cursor_pos);
        var p2 = skip_word_forward(p1);
        var p4 = skip_word_forward(cursor_pos);
        var p3 = skip_word_backward(p4);

        if (p1 < p2 && p2 <= cursor_pos && cursor_pos <= p3 && p3 < p4) {
            cmd = cmd.substring(0, p1) + cmd.substring(p3, p4) +
            cmd.substring(p2, p3) + cmd.substring(p1, p2);
            cursor_pos = p4;
        }
    }

    function upcase_word() {
        var end = skip_word_forward(cursor_pos);
        cmd = cmd.substring(0, cursor_pos) +
            cmd.substring(cursor_pos, end).toUpperCase() +
            cmd.substring(end);
    }

    function downcase_word() {
        var end = skip_word_forward(cursor_pos);
        cmd = cmd.substring(0, cursor_pos) +
            cmd.substring(cursor_pos, end).toLowerCase() +
            cmd.substring(end);
    }

    function kill_region(start, end, dir) {
        var s = cmd.substring(start, end);
        if (last_fun !== kill_region)
            clip_board = s;
        else if (dir < 0)
            clip_board = s + clip_board;
        else
            clip_board = clip_board + s;

        cmd = cmd.substring(0, start) + cmd.substring(end);
        if (cursor_pos > end)
            cursor_pos -= end - start;
        else if (cursor_pos > start)
            cursor_pos = start;
        this_fun = kill_region;
    }

    function kill_line() {
        kill_region(cursor_pos, cmd.length, 1);
    }

    function backward_kill_line() {
        kill_region(0, cursor_pos, -1);
    }

    function kill_word() {
        kill_region(cursor_pos, skip_word_forward(cursor_pos), 1);
    }

    function backward_kill_word() {
        kill_region(skip_word_backward(cursor_pos), cursor_pos, -1);
    }

    function yank() {
        insert(clip_board);
    }

    function control_c() {
        if (last_fun === control_c) {
            std.puts("\n");
            std.exit(0);
        } else {
            std.puts("\n(Press Ctrl-C again to quit)\n");
            readline_print_prompt();
        }
    }
    
    function reset() {
        cmd = "";
        cursor_pos = 0;
    }

    function get_context_word(line, pos) {
        var s = "";
        while (pos > 0 && is_word(line[pos - 1])) {
            pos--;
            s = line[pos] + s;
        }
        return s;
    }
    function get_context_object(line, pos) {
        var obj, base, c;
        if (pos <= 0 || " ~!%^&*(-+={[|:;,<>?/".indexOf(line[pos - 1]) >= 0)
            return g;
        if (pos >= 2 && line[pos - 1] === ".") {
            pos--;
            obj = {};
            switch (c = line[pos - 1]) {
            case '\'':
            case '\"':
                return "a";
            case ']':
                return [];
            case '}':
                return {};
            case '/':
                return / /;
            default:
                if (is_word(c)) {
                    base = get_context_word(line, pos);
                    if (["true", "false", "null", "this"].includes(base) || !isNaN(+base))
                        return eval(base);
                    obj = get_context_object(line, pos - base.length);
                    if (obj === null || obj === void 0)
                        return obj;
                    if (obj === g && obj[base] === void 0)
                        return eval(base);
                    else
                        return obj[base];
                }
                return {};
            }
        }
        return void 0;
    }

    function get_completions(line, pos) {
        var s, obj, ctx_obj, r, i, j, paren;

        s = get_context_word(line, pos);
        ctx_obj = get_context_object(line, pos - s.length);
        r = [];
        /*  从对象及其原型链中枚举属性，添加以%s作为e前缀的非数字常规属性。 */ 
        for (i = 0, obj = ctx_obj; i < 10 && obj !== null && obj !== void 0; i++) {
            var props = Object.getOwnPropertyNames(obj);
            /*  添加非数字常规属性。 */ 
            for (j = 0; j < props.length; j++) {
                var prop = props[j];
                if (typeof prop == "string" && ""+(+prop) != prop && prop.startsWith(s))
                    r.push(prop);
            }
            obj = Object.getPrototypeOf(obj);
        }
        if (r.length > 1) {
            /*  将内部名称排在最后的列表并删除重复项。 */ 
            function symcmp(a, b) {
                if (a[0] != b[0]) {
                    if (a[0] == '_')
                        return 1;
                    if (b[0] == '_')
                        return -1;
                }
                if (a < b)
                    return -1;
                if (a > b)
                    return +1;
                return 0;
            }
            r.sort(symcmp);
            for(i = j = 1; i < r.length; i++) {
                if (r[i] != r[i - 1])
                    r[j++] = r[i];
            }
            r.length = j;
        }
        /*  ‘Tab’=完成列表，‘pos’=光标位置完成度。 */ 
        return { tab: r, pos: s.length, ctx: ctx_obj };
    }

    function completion() {
        var tab, res, s, i, j, len, t, max_width, col, n_cols, row, n_rows;
        res = get_completions(cmd, cursor_pos);
        tab = res.tab;
        if (tab.length === 0)
            return;
        s = tab[0];
        len = s.length;
        /*  添加在所有补全中相同的字符。 */ 
        for(i = 1; i < tab.length; i++) {
            t = tab[i];
            for(j = 0; j < len; j++) {
                if (t[j] !== s[j]) {
                    len = j;
                    break;
                }
            }
        }
        for(i = res.pos; i < len; i++) {
            insert(s[i]);
        }
        if (last_fun === completion && tab.length == 1) {
            /*  在函数名后附加圆括号。 */ 
            var m = res.ctx[tab[0]];
            if (typeof m == "function") {
                insert('(');
                if (m.length == 0)
                    insert(')');
            } else if (typeof m == "object") {
                insert('.');
            }
        }
        /*  显示可能完成的项目。 */ 
        if (last_fun === completion && tab.length >= 2) {
            max_width = 0;
            for(i = 0; i < tab.length; i++)
                max_width = Math.max(max_width, tab[i].length);
            max_width += 2;
            n_cols = Math.max(1, Math.floor((term_width + 1) / max_width));
            n_rows = Math.ceil(tab.length / n_cols);
            std.puts("\n");
            /*  按列显示已排序的列表。 */ 
            for (row = 0; row < n_rows; row++) {
                for (col = 0; col < n_cols; col++) {
                    i = col * n_rows + row;
                    if (i >= tab.length)
                        break;
                    s = tab[i];
                    if (col != n_cols - 1)
                        s = s.padEnd(max_width);
                    std.puts(s);
                }
                std.puts("\n");
            }
            /*  显示新提示。 */ 
            readline_print_prompt();
        }
    }
    
    var commands = {        /*  命令表。 */ 
        "\x01":     beginning_of_line,      /* ^A - bol */
        "\x02":     backward_char,          /* ^B - backward-char */
        "\x03":     control_c,              /* ^C - abort */
        "\x04":     control_d,              /* ^D - delete-char or exit */
        "\x05":     end_of_line,            /* ^E - eol */
        "\x06":     forward_char,           /* ^F - forward-char */
        "\x07":     abort,                  /* ^G - bell */
        "\x08":     backward_delete_char,   /* ^H - backspace */
        "\x09":     completion,             /* ^I - history-search-backward */
        "\x0a":     accept_line,            /* ^J - newline */
        "\x0b":     kill_line,              /* ^K - delete to end of line */
        "\x0d":     accept_line,            /* ^M - enter */
        "\x0e":     next_history,           /* ^N - down */
        "\x10":     previous_history,       /* ^P - up */
        "\x11":     quoted_insert,          /* ^Q - quoted-insert */
        "\x12":     alert,                  /* ^R - reverse-search */
        "\x13":     alert,                  /* ^S - search */
        "\x14":     transpose_chars,        /* ^T - transpose */
        "\x18":     reset,                  /* ^X - cancel */
        "\x19":     yank,                   /* ^Y - yank */
        "\x1bOA":   previous_history,       /* ^[OA - up */
        "\x1bOB":   next_history,           /* ^[OB - down */
        "\x1bOC":   forward_char,           /* ^[OC - right */
        "\x1bOD":   backward_char,          /* ^[OD - left */
        "\x1bOF":   forward_word,           /* ^[OF - ctrl-right */
        "\x1bOH":   backward_word,          /* ^[OH - ctrl-left */
        "\x1b[1;5C": forward_word,          /* ^[[1;5C - ctrl-right */
        "\x1b[1;5D": backward_word,         /* ^[[1;5D - ctrl-left */
        "\x1b[1~":  beginning_of_line,      /* ^[[1~ - bol */
        "\x1b[3~":  delete_char,            /* ^[[3~ - delete */
        "\x1b[4~":  end_of_line,            /* ^[[4~ - eol */
        "\x1b[5~":  history_search_backward,/* ^[[5~ - page up */
        "\x1b[6~":  history_search_forward, /* ^[[5~ - page down */
        "\x1b[A":   previous_history,       /* ^[[A - up */
        "\x1b[B":   next_history,           /* ^[[B - down */
        "\x1b[C":   forward_char,           /* ^[[C - right */
        "\x1b[D":   backward_char,          /* ^[[D - left */
        "\x1b[F":   end_of_line,            /* ^[[F - end */
        "\x1b[H":   beginning_of_line,      /* ^[[H - home */
        "\x1b\x7f": backward_kill_word,     /* M-C-? - backward_kill_word */
        "\x1bb":    backward_word,          /* M-b - backward_word */
        "\x1bd":    kill_word,              /* M-d - kill_word */
        "\x1bf":    forward_word,           /* M-f - backward_word */
        "\x1bk":    backward_kill_line,     /* M-k - backward_kill_line */
        "\x1bl":    downcase_word,          /* M-l - downcase_word */
        "\x1bt":    transpose_words,        /* M-t - transpose_words */
        "\x1bu":    upcase_word,            /* M-u - upcase_word */
        "\x7f":     backward_delete_char,   /* ^? - delete */
    };

    function dupstr(str, count) {
        var res = "";
        while (count-- > 0)
            res += str;
        return res;
    }

    var readline_keys;
    var readline_state;
    var readline_cb;

    function readline_print_prompt()
    {
        std.puts(prompt);
        term_cursor_x = prompt.length % term_width;
        last_cmd = "";
        last_cursor_pos = 0;
    }

    function readline_start(defstr, cb) {
        cmd = defstr || "";
        cursor_pos = cmd.length;
        history_index = history.length;
        readline_cb = cb;
        
        prompt = pstate;
    
        if (mexpr) {
            prompt += dupstr(" ", plen - prompt.length);
            prompt += ps2;
        } else {
            if (show_time) {
                var t = Math.round(eval_time) + " ";
                eval_time = 0;
                t = dupstr("0", 5 - t.length) + t;
                prompt += t.substring(0, t.length - 4) + "." + t.substring(t.length - 4);
            }
            plen = prompt.length;
            prompt += ps1;
        }
        readline_print_prompt();
        update();
        readline_state = 0;
    }

    function handle_char(c1) {
        var c;
        c = String.fromCharCode(c1);
        switch(readline_state) {
        case 0:
            if (c == '\x1b') {  /*  ‘^[’-Esc。 */ 
                readline_keys = c;
                readline_state = 1;
            } else {
                handle_key(c);
            }
            break;
        case 1: /*  ‘^[。 */ 
            readline_keys += c;
            if (c == '[') {
                readline_state = 2;
            } else if (c == 'O') {
                readline_state = 3;
            } else {
                handle_key(readline_keys);
                readline_state = 0;
            }
            break;
        case 2: /*  ‘^[[’-CSI。 */ 
            readline_keys += c;
            if (!(c == ';' || (c >= '0' && c <= '9'))) {
                handle_key(readline_keys);
                readline_state = 0;
            }
            break;
        case 3: /*  ‘^[O’-ESc2。 */ 
            readline_keys += c;
            handle_key(readline_keys);
            readline_state = 0;
            break;
        }
    }

    function handle_key(keys) {
        var fun;

        if (quote_flag) {
            if (keys.length === 1)
                insert(keys);
            quote_flag = false;
        } else if (fun = commands[keys]) {
            this_fun = fun;
            switch (fun(keys)) {
            case -1:
                readline_cb(cmd);
                return;
            case -2:
                readline_cb(null);
                return;
            case -3:
                /*  卸载Ctrl-C信号处理程序。 */ 
                os.signal(os.SIGINT, null);
                /*  卸载stdin读取处理程序。 */ 
                os.setReadHandler(term_fd, null);
                return;
            }
            last_fun = this_fun;
        } else if (keys.length === 1 && keys >= ' ') {
            insert(keys);
            last_fun = insert;
        } else {
            alert(); /*  哔！ */ 
        }
        
        cursor_pos = (cursor_pos < 0) ? 0 :
            (cursor_pos > cmd.length) ? cmd.length : cursor_pos;
        update();
    }

    var hex_mode = false;
    var eval_mode = "std";

    function bignum_typeof(a) {
        "use bigint";
        return typeof a;
    }

    function eval_mode_typeof(a) {
        if (eval_mode === "std")
            return typeof a;
        else
            return bignum_typeof(a);
    }
    
    function number_to_string(a, radix) {
        var s;
        if (!isFinite(a)) {
            /*  南，无限。 */ 
            if (typeof a === "bigfloat" && eval_mode !== "math") {
                return "BigFloat(" + a.toString() + ")";
            } else {
                return a.toString();
            }
        } else {
            if (a == 0) {
                if (1 / a < 0)
                    s = "-0";
                else
                    s = "0";
            } else {
                if (radix == 16) {
                    var s;
                    if (a < 0) {
                        a = -a;
                        s = "-";
                    } else {
                        s = "";
                    }
                    s += "0x" + a.toString(16);
                } else {
                    s = a.toString();
                }
            }
            if (typeof a === "bigfloat" && eval_mode !== "math") {
                s += "l";
            } else if (eval_mode !== "std" && s.indexOf(".") < 0 &&
                ((radix == 16 && s.indexOf("p") < 0) ||
                 (radix == 10 && s.indexOf("e") < 0))) {
                /*  添加一个小数点，以便浮点类型是可见的。 */ 
                s += ".0";
            }
            return s;
        }
    }

    function bigint_to_string(a, radix) {
        var s;
        if (radix == 16) {
            var s;
            if (a < 0) {
                a = -a;
                s = "-";
            } else {
                s = "";
            }
            s += "0x" + a.toString(16);
        } else {
            s = a.toString();
        }
        if (eval_mode === "std")
            s += "n";
        return s;
    }
    
    function print(a) {
        var stack = [];

        function print_rec(a) {
            var n, i, keys, key, type, s;
            
            type = eval_mode_typeof(a);
            if (type === "object") {
                if (a === null) {
                    std.puts(a);
                } else if (stack.indexOf(a) >= 0) {
                    std.puts("[circular]");
                } else if (has_jscalc && (a instanceof Fraction ||
                                        a instanceof Complex ||
                                        a instanceof Mod ||
                                        a instanceof Polynomial ||
                                        a instanceof PolyMod ||
                                        a instanceof RationalFunction ||
                                        a instanceof Series)) {
                    std.puts(a.toString());
                } else {
                    stack.push(a);
                    if (Array.isArray(a)) {
                        n = a.length;
                        std.puts("[ ");
                        for(i = 0; i < n; i++) {
                            if (i !== 0)
                                std.puts(", ");
                            if (i in a) {
                                print_rec(a[i]);
                            } else {
                                std.puts("<empty>");
                            }
                            if (i > 20) {
                                std.puts("...");
                                break;
                            }
                        }
                        std.puts(" ]");
                    } else if (Object.__getClass(a) === "RegExp") {
                        std.puts(a.toString());
                    } else {
                        keys = Object.keys(a);
                        n = keys.length;
                        std.puts("{ ");
                        for(i = 0; i < n; i++) {
                            if (i !== 0)
                                std.puts(", ");
                            key = keys[i];
                            std.puts(key, ": ");
                            print_rec(a[key]);
                        }
                        std.puts(" }");
                    }
                    stack.pop(a);
                }
            } else if (type === "string") {
                s = a.__quote();
                if (s.length > 79)
                    s = s.substring(0, 75) + "...\"";
                std.puts(s);
            } else if (type === "number" || type === "bigfloat") {
                std.puts(number_to_string(a, hex_mode ? 16 : 10));
            } else if (type === "bigint") {
                std.puts(bigint_to_string(a, hex_mode ? 16 : 10));
            } else if (type === "symbol") {
                std.puts(String(a));
            } else if (type === "function") {
                std.puts("function " + a.name + "()");
            } else {
                std.puts(a);
            }
        }
        print_rec(a);
    }
    
    function extract_directive(a) {
        var pos;
        if (a[0] !== '\\')
            return "";
        for (pos = 1; pos < a.length; pos++) {
            if (!is_alpha(a[pos]))
                break;
        }
        return a.substring(1, pos);
    }

    /*  如果cmd后的字符串可以计算为JS，则返回TRUE。 */ 
    function handle_directive(cmd, expr) {
        var param, prec1, expBits1;
        
        if (cmd === "h" || cmd === "?" || cmd == "help") {
            help();
        } else if (cmd === "load") {
            var filename = expr.substring(cmd.length + 1).trim();
            if (filename.lastIndexOf(".") <= filename.lastIndexOf("/"))
                filename += ".js";
            std.loadScript(filename);
            return false;
        } else if (cmd === "x") {
            hex_mode = true;
        } else if (cmd === "d") {
            hex_mode = false;
        } else if (cmd === "t") {
            show_time = !show_time;
        } else if (has_bignum && cmd === "p") {
            param = expr.substring(cmd.length + 1).trim().split(" ");
            if (param.length === 1 && param[0] === "") {
                std.puts("BigFloat precision=" + prec + " bits (~" +
                          Math.floor(prec / log2_10) +
                          " digits), exponent size=" + expBits + " bits\n");
            } else if (param[0] === "f16") {
                prec = 11;
                expBits = 5;
            } else if (param[0] === "f32") {
                prec = 24;
                expBits = 8;
            } else if (param[0] === "f64") {
                prec = 53;
                expBits = 11;
            } else if (param[0] === "f128") {
                prec = 113;
                expBits = 15;
            } else {
                prec1 = parseInt(param[0]);
                if (param.length >= 2)
                    expBits1 = parseInt(param[1]);
                else
                    expBits1 = BigFloatEnv.expBitsMax;
                if (Number.isNaN(prec1) ||
                    prec1 < BigFloatEnv.precMin ||
                    prec1 > BigFloatEnv.precMax) {
                    std.puts("Invalid precision\n");
                    return false;
                }
                if (Number.isNaN(expBits1) ||
                    expBits1 < BigFloatEnv.expBitsMin ||
                    expBits1 > BigFloatEnv.expBitsMax) {
                    std.puts("Invalid exponent bits\n");
                    return false;
                }
                prec = prec1;
                expBits = expBits1;
            }
            return false;
        } else if (has_bignum && cmd === "digits") {
            param = expr.substring(cmd.length + 1).trim();
            prec1 = Math.ceil(parseFloat(param) * log2_10);
            if (prec1 < BigFloatEnv.precMin ||
                prec1 > BigFloatEnv.precMax) {
                std.puts("Invalid precision\n");
                return false;
            }
            prec = prec1;
            expBits = BigFloatEnv.expBitsMax;
            return false;
        } else if (has_bignum && cmd === "mode") {
            param = expr.substring(cmd.length + 1).trim();
            if (param === "") {
                std.puts("Running mode=" + eval_mode + "\n");
            } else if (param === "std" || param === "math" ||
                       param === "bigint") {
                eval_mode = param;
            } else {
                std.puts("Invalid mode\n");
            }
            return false;
        } else if (cmd === "clear") {
            std.puts("\x1b[H\x1b[J");
        } else if (cmd === "q") {
            std.exit(0);
        } else if (has_jscalc && cmd === "a") {
            algebraicMode = true;
        } else if (has_jscalc && cmd === "n") {
            algebraicMode = false;
        } else {
            std.puts("Unknown directive: " + cmd + "\n");
            return false;
        }
        return true;
    }

    if (config_numcalc) {
        /*  由图形用户界面调用。 */ 
        g.execCmd = function (cmd) {
            switch(cmd) {
            case "dec":
                hex_mode = false;
                break;
            case "hex":
                hex_mode = true;
                break;
            case "num":
                algebraicMode = false;
                break;
            case "alg":
                algebraicMode = true;
                break;
            }
        }
    }
    
    function help() {
        function sel(n) {
            return n ? "*": " ";
        }
        std.puts("\\h          this help\n" +
                 "\\x         " + sel(hex_mode) + "hexadecimal number display\n" +
                 "\\d         " + sel(!hex_mode) + "decimal number display\n" +
                 "\\t         " + sel(show_time) + "toggle timing display\n" +
                  "\\clear      clear the terminal\n");
        if (has_jscalc) {
            std.puts("\\a         " + sel(algebraicMode) + "algegraic mode\n" +
                     "\\n         " + sel(!algebraicMode) + "numeric mode\n");
        }
        if (has_bignum) {
            std.puts("\\p [m [e]]  set the BigFloat precision to 'm' bits\n" +
                     "\\digits n   set the BigFloat precision to 'ceil(n*log2(10))' bits\n");
            if (!has_jscalc) {
                std.puts("\\mode [std|bigint|math] change the running mode (current = " + eval_mode + ")\n");
            }
        }
        if (!config_numcalc) {
            std.puts("\\q          exit\n");
        }
    }

    function eval_and_print(expr) {
        var result;
        
        try {
            if (eval_mode === "math")
                expr = '"use math"; void 0;' + expr;
            else if (eval_mode === "bigint")
                expr = '"use bigint"; void 0;' + expr;
            var now = (new Date).getTime();
            /*  作为剧本的Eval。 */ 
            result = std.evalScript(expr);
            eval_time = (new Date).getTime() - now;
            std.puts(colors[styles.result]);
            print(result);
            std.puts("\n");
            std.puts(colors.none);
            /*  设置最后一个结果。 */ 
            g._ = result;
        } catch (error) {
            std.puts(colors[styles.error_msg]);
            if (error instanceof Error) {
                console.log(error);
                if (error.stack) {
                    std.puts(error.stack);
                }
            } else {
                std.puts("Throw: ");
                console.log(error);
            }
            std.puts(colors.none);
        }
    }

    function cmd_start() {
        if (!config_numcalc) {
            if (has_jscalc)
                std.puts('QJSCalc - Type "\\h" for help\n');
            else
                std.puts('QuickJS - Type "\\h" for help\n');
        }
        if (has_bignum) {
            log2_10 = Math.log(10) / Math.log(2);
            if (has_jscalc) {
                prec = 113;
                expBits = 15;
                eval_mode = "math";
                /*  XXX：数字模式是否应始终为默认模式？ */ 
                g.algebraicMode = config_numcalc;
            } else {
                prec = 53;
                expBits = 11;
            }
        }

        cmd_readline_start();
    }

    function cmd_readline_start() {
        readline_start(dupstr("    ", level), readline_handle_cmd);
    }
    
    function readline_handle_cmd(expr) {
        handle_cmd(expr);
        cmd_readline_start();
    }

    function handle_cmd(expr) {
        var colorstate, cmd;
        
        if (expr === null) {
            expr = "";
            return;
        }
        if (expr === "?") {
            help();
            return;
        }
        cmd = extract_directive(expr);
        if (cmd.length > 0) {
            if (!handle_directive(cmd, expr))
                return;
            expr = expr.substring(cmd.length + 1);
        }
        if (expr === "")
            return;
        
        if (mexpr)
            expr = mexpr + '\n' + expr;
        colorstate = colorize_js(expr);
        pstate = colorstate[0];
        level = colorstate[1];
        if (pstate) {
            mexpr = expr;
            return;
        }
        mexpr = "";
        
        if (has_bignum) {
            BigFloatEnv.setPrec(eval_and_print.bind(null, expr),
                                prec, expBits);
        } else {
            eval_and_print(expr);
        }
        level = 0;
        
        /*  在每个命令后运行垃圾收集器。 */ 
        std.gc();
    }

    function colorize_js(str) {
        var i, c, start, n = str.length;
        var style, state = "", level = 0;
        var primary, can_regex = 1;
        var r = [];

        function push_state(c) { state += c; }
        function last_state(c) { return state.substring(state.length - 1); }
        function pop_state(c) {
            var c = last_state();
            state = state.substring(0, state.length - 1);
            return c;
        }

        function parse_block_comment() {
            style = 'comment';
            push_state('/');
            for (i++; i < n - 1; i++) {
                if (str[i] == '*' && str[i + 1] == '/') {
                    i += 2;
                    pop_state('/');
                    break;
                }
            }
        }

        function parse_line_comment() {
            style = 'comment';
            for (i++; i < n; i++) {
                if (str[i] == '\n') {
                    break;
                }
            }
        }

        function parse_string(delim) {
            style = 'string';
            push_state(delim);
            while (i < n) {
                c = str[i++];
                if (c == '\n') {
                    style = 'error';
                    continue;
                }
                if (c == '\\') {
                    if (i >= n)
                        break;
                    i++;
                } else
                if (c == delim) {
                    pop_state();
                    break;
                }
            }
        }

        function parse_regex() {
            style = 'regex';
            push_state('/');
            while (i < n) {
                c = str[i++];
                if (c == '\n') {
                    style = 'error';
                    continue;
                }
                if (c == '\\') {
                    if (i < n) {
                        i++;
                    }
                    continue;
                }
                if (last_state() == '[') {
                    if (c == ']') {
                        pop_state()
                    }
                    //  ECMA 5：忽略字符类中的‘/’
                    continue;
                }
                if (c == '[') {
                    push_state('[');
                    if (str[i] == '[' || str[i] == ']')
                        i++;
                    continue;
                }
                if (c == '/') {
                    pop_state();
                    while (i < n && is_word(str[i]))
                        i++;
                    break;
                }
            }
        }

        function parse_number() {
            style = 'number';
            while (i < n && (is_word(str[i]) || (str[i] == '.' && (i == n - 1 || str[i + 1] != '.')))) {
                i++;
            }
        }

        var js_keywords = "|" +
            "break|case|catch|continue|debugger|default|delete|do|" +
            "else|finally|for|function|if|in|instanceof|new|" +
            "return|switch|this|throw|try|typeof|while|with|" +
            "class|const|enum|import|export|extends|super|" +
            "implements|interface|let|package|private|protected|" +
            "public|static|yield|" +
            "undefined|null|true|false|Infinity|NaN|" +
            "eval|arguments|" +
            "await|";

        var js_no_regex = "|this|super|undefined|null|true|false|Infinity|NaN|arguments|";
        var js_types = "|void|var|";

        function parse_identifier() {
            can_regex = 1;

            while (i < n && is_word(str[i]))
                i++;

            var w = '|' + str.substring(start, i) + '|';

            if (js_keywords.indexOf(w) >= 0) {
                style = 'keyword';
                if (js_no_regex.indexOf(w) >= 0)
                    can_regex = 0;
                return;
            }

            var i1 = i;
            while (i1 < n && str[i1] == ' ')
                i1++;

            if (i1 < n && str[i1] == '(') {
                style = 'function';
                return;
            }

            if (js_types.indexOf(w) >= 0) {
                style = 'type';
                return;
            }

            style = 'identifier';
            can_regex = 0;
        }

        function set_style(from, to) {
            while (r.length < from)
                r.push('default');
            while (r.length < to)
                r.push(style);
        }

        for (i = 0; i < n;) {
            style = null;
            start = i;
            switch (c = str[i++]) {
            case ' ':
            case '\t':
            case '\r':
            case '\n':
                continue;
            case '+':
            case '-':
                if (i < n && str[i] == c) {
                    i++;
                    continue;
                }
                can_regex = 1;
                continue;
            case '/':
                if (i < n && str[i] == '*') { //  阻止注释。
                    parse_block_comment();
                    break;
                }
                if (i < n && str[i] == '/') { //  行注释
                    parse_line_comment();
                    break;
                }
                if (can_regex) {
                    parse_regex();
                    can_regex = 0;
                    break;
                }
                can_regex = 1;
                continue;
            case '\'':
            case '\"':
            case '`':
                parse_string(c);
                can_regex = 0;
                break;
            case '(':
            case '[':
            case '{':
                can_regex = 1;
                level++;
                push_state(c);
                continue;
            case ')':
            case ']':
            case '}':
                can_regex = 0;
                if (level > 0 && is_balanced(last_state(), c)) {
                    level--;
                    pop_state();
                    continue;
                }
                style = 'error';
                break;
            default:
                if (is_digit(c)) {
                    parse_number();
                    can_regex = 0;
                    break;
                }
                if (is_word(c) || c == '$') {
                    parse_identifier();
                    break;
                }
                can_regex = 1;
                continue;
            }
            if (style)
                set_style(start, i);
        }
        set_style(n, n);
        return [ state, level, r ];
    }

    termInit();
    
    cmd_start();

})(globalThis);
