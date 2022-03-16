/*
 * @file url.c
 * @author Geng Yue
 * @date 2021/12/26
 * @brief The implementation of URL implementation.
 *
 * Copyright (C) 2021 FMSoft <https://www.fmsoft.cn>
 *
 * This file is a part of PurC (short for Purring Cat), an HVML interpreter.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "purc-utils.h"

#include "wtf/URL.h"
#include "wtf/ASCIICType.h"
#include "wtf/text/StringBuilder.h"

char *pcutils_url_assemble(const struct purc_broken_down_url *url_struct)
{
    char * url_string = NULL;
    String string = "";
    WTF::URL url(WTF::URL(), string);

    if (url_struct->schema)
        url.setProtocol(url_struct->schema);

    if (url_struct->host)
        url.setHost(url_struct->host);

    if (url_struct->port)
        url.setPort(url_struct->port);

    if (url_struct->path)
        url.setPath(url_struct->path);

    if (url_struct->query)
        url.setQuery(url_struct->query);

    if (url_struct->fragment)
        url.setFragmentIdentifier(url_struct->fragment);

    if (url_struct->user)
        url.setUser(url_struct->user);

    if (url_struct->passwd)
        url.setPassword(url_struct->passwd);

    if (url.isValid()) {
        String tempstring = url.string();
// if you wana decode URL, use the line below
// String tempstring = WTF::decodeEscapeSequencesFromParsedURL(url.string());
        url_string = strdup(tempstring.latin1().data());
    }

    return url_string;
}

// escape char which is grater than 127.
// whether other char is valid, URL parser will do it.
static bool shouldEncode(unsigned char c)
{
    if (c > 127)
        return true;
    else
        return false;
}

static String percentEncodeCharacters(const unsigned char * data)
{
    WTF::StringBuilder builder;
    auto length = strlen ((char *)data);
    for (unsigned j = 0; j < length; j++) {
        auto c = data[j];
        if (shouldEncode(c)) {
            builder.append('%');
            builder.append(upperNibbleToASCIIHexDigit(c));
            builder.append(lowerNibbleToASCIIHexDigit(c));
        } else
            builder.append(c);
    }
    return builder.toString();
}

bool pcutils_url_break_down(struct purc_broken_down_url *url_struct,
        const char *url_string)
{
    // std::unique_ptr<WTF::URL> url = makeUnique<URL>(URL(), url_string);
    String encode_url = percentEncodeCharacters((const unsigned char *)url_string);
    WTF::URL url(URL(), encode_url);

    bool valid = url.isValid();
    size_t length = 0;
    const char *tempstring = NULL;

    if (valid) {
        if (url_struct->schema)
            free(url_struct->schema);
        StringView protocol = url.protocol();
        CString latin1 = protocol.toString().latin1();
        tempstring = latin1.data();
        length = strlen(tempstring);
        if (length)
            url_struct->schema = strdup(tempstring);
        else
            url_struct->schema = NULL;

        if (url_struct->user)
            free(url_struct->user);
        String user = url.user();
        latin1 = user.latin1();
        tempstring = latin1.data();
        length = strlen(tempstring);
        if (length)
            url_struct->user = strdup(tempstring);
        else
            url_struct->user = NULL;

        if (url_struct->passwd)
            free(url_struct->passwd);
        String password = url.password();
        latin1 = password.latin1();
        tempstring = latin1.data();
        length = strlen(tempstring);
        if (length)
            url_struct->passwd = strdup(tempstring);
        else
            url_struct->passwd = NULL;

        if (url_struct->host)
            free(url_struct->host);
        StringView host = url.host();
        latin1 = host.toString().latin1();
        tempstring = latin1.data();
        length = strlen(tempstring);
        if (length)
            url_struct->host = strdup(tempstring);
        else
            url_struct->host = NULL;

        if (url_struct->path)
            free (url_struct->path);
        StringView path = url.path();
        latin1 = path.toString().latin1();
        tempstring = latin1.data();
        length = strlen(tempstring);
        if (length)
            url_struct->path = strdup(tempstring);
        else
            url_struct->path = NULL;

        if (url_struct->query)
            free(url_struct->query);
        StringView query = url.query();
        latin1 = query.toString().latin1();
        tempstring = latin1.data();
        length = strlen(tempstring);
        if (length)
            url_struct->query = strdup(tempstring);
        else
            url_struct->query = NULL;

        if (url_struct->fragment)
            free(url_struct->fragment);
        StringView fragmentIdentifier = url.fragmentIdentifier();
        latin1 = fragmentIdentifier.toString().latin1();
        tempstring = latin1.data();
        length = strlen(tempstring);
        if (length)
            url_struct->fragment = strdup(tempstring);
        else
            url_struct->fragment = NULL;

        Optional<uint16_t> port = url.port();
        if (port)
            url_struct->port = (unsigned int)(*port);
        else
            url_struct->port = 0;
    }

    return valid;
}

