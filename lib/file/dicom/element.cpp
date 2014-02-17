#include "file/path.h"
#include "file/dicom/element.h"
#include "get_set.h"
#include "debug.h"

namespace MR {
  namespace File {
    namespace Dicom {

      void Element::set (const std::string& filename, bool force_read, bool read_write)
      {
        group = element = VR = 0;
        size = 0;
        start = data = next = NULL;
        is_BE = is_transfer_syntax_BE = false;
        parents.clear();

        fmap = new File::MMap (filename, read_write);

        if (fmap->size() < 256) 
          throw Exception ("\"" + fmap->name() + "\" is too small to be a valid DICOM file");

        next = fmap->address();

        if (memcmp (next + 128, "DICM", 4)) {
          is_explicit = false;
          DEBUG ("DICOM magic number not found in file \"" + fmap->name() + "\" - trying truncated format");
          if (!force_read)
            if (!Path::has_suffix (fmap->name(), ".dcm")) 
              throw Exception ("file \"" + fmap->name() + "\" does not have the DICOM magic number or the .dcm extension - assuming not DICOM");
        }
        else next += 132;

        try { set_explicit_encoding(); }
        catch (Exception) {
          throw Exception ("\"" + fmap->name() + "\" is not a valid DICOM file");
          fmap = NULL;
        }
      }






      void Element::set_explicit_encoding ()
      {
        assert (fmap);
        if (read_GR_EL()) 
          throw Exception ("\"" + fmap->name() + "\" is too small to be DICOM");

        is_explicit = true;
        next = start;
        VR = ByteOrder::BE (*reinterpret_cast<uint16_t*> (start+4));

        if ((VR == VR_OB) | (VR == VR_OW) | (VR == VR_OF) | (VR == VR_SQ) |
            (VR == VR_UN) | (VR == VR_AE) | (VR == VR_AS) | (VR == VR_AT) |
            (VR == VR_CS) | (VR == VR_DA) | (VR == VR_DS) | (VR == VR_DT) |
            (VR == VR_FD) | (VR == VR_FL) | (VR == VR_IS) | (VR == VR_LO) |
            (VR == VR_LT) | (VR == VR_PN) | (VR == VR_SH) | (VR == VR_SL) |
            (VR == VR_SS) | (VR == VR_ST) | (VR == VR_TM) | (VR == VR_UI) |
            (VR == VR_UL) | (VR == VR_US) | (VR == VR_UT)) return;

        DEBUG ("using implicit DICOM encoding");
        is_explicit = false;
      }






      bool Element::read_GR_EL ()
      {
        group = element = VR = 0;
        size = 0;
        start = next;
        data = next = NULL;

        if (start < fmap->address())
          throw Exception ("invalid DICOM element");

        if (start + 8 > fmap->address() + fmap->size()) 
          return true;

        is_BE = is_transfer_syntax_BE;

        group = get<uint16_t> (start, is_BE);

        if (group == GROUP_BYTE_ORDER_SWAPPED) {
          if (!is_BE) 
            throw Exception ("invalid DICOM group ID " + str (group) + " in file \"" + fmap->name() + "\"");

          is_BE = false;
          group = GROUP_BYTE_ORDER;
        }
        element = get<uint16_t> (start+2, is_BE);

        return false;
      }






      bool Element::read ()
      {
        if (read_GR_EL()) 
          return false;

        data = start + 8;
        if ((is_explicit && group != GROUP_SEQUENCE) || group == GROUP_BYTE_ORDER) {

          // explicit encoding:
          VR = ByteOrder::BE (*reinterpret_cast<uint16_t*> (start+4));
          if (VR == VR_OB || VR == VR_OW || VR == VR_OF || VR == VR_SQ || VR == VR_UN || VR == VR_UT) {
            size = get<uint32_t> (start+8, is_BE);
            data += 4;
          }
          else size = get<uint16_t> (start+6, is_BE);

        }
        else {

          // implicit encoding:
          std::string name = tag_name();
          if (!name.size()) {
            if (group%2 == 0) 
              DEBUG (printf ("WARNING: unknown DICOM tag (%02X %02X) "
                    "with implicit encoding in file \"", group, element) 
                  + fmap->name() + "\"");
            VR = VR_UN;
          }
          else {
            union { 
              char t[2];
              uint16_t i;
            } d = { { name[0], name[1] } };
            VR = ByteOrder::BE (d.i);
          }
          size = get<uint32_t> (start+4, is_BE);
        }


        next = data;
        if (size == LENGTH_UNDEFINED) {
          if (VR != VR_SQ && !(group == GROUP_SEQUENCE && element == ELEMENT_SEQUENCE_ITEM)) 
            throw Exception ("undefined length used for DICOM tag " + ( tag_name().size() ? tag_name().substr (2) : "" ) 
                + " (" + str (group) + ", " + str (element) 
                + ") in file \"" + fmap->name() + "\"");
        }
        else if (next+size > fmap->address() + fmap->size()) 
          throw Exception ("file \"" + fmap->name() + "\" is too small to contain DICOM elements specified");
        else if (size%2) 
          throw Exception ("odd length (" + str (size) + ") used for DICOM tag " + ( tag_name().size() ? tag_name().substr (2) : "" ) 
              + " (" + str (group) + ", " + str (element) + ") in file \"" + fmap->name() + "");
        else if (VR != VR_SQ && ( group != GROUP_SEQUENCE || element != ELEMENT_SEQUENCE_ITEM ) ) 
          next += size;



        if (parents.size()) 
          if ((parents.back().end && data > parents.back().end) || 
              (group == GROUP_SEQUENCE && element == ELEMENT_SEQUENCE_DELIMITATION_ITEM)) 
            parents.pop_back();

        if (VR == VR_SQ) {
          if (size == LENGTH_UNDEFINED) 
            parents.push_back (Sequence (group, element, NULL)); 
          else 
            parents.push_back (Sequence (group, element, data + size));
        }




        switch (group) {
          case GROUP_BYTE_ORDER:
            switch (element) {
              case ELEMENT_TRANSFER_SYNTAX_UID:
                if (strncmp (reinterpret_cast<const char*> (data), "1.2.840.10008.1.2.1", size) == 0) {
                  is_BE = is_transfer_syntax_BE = false; // explicit VR Little Endian
                  is_explicit = true;
                }
                else if (strncmp (reinterpret_cast<const char*> (data), "1.2.840.10008.1.2.2", size) == 0) {
                  is_BE = is_transfer_syntax_BE = true; // Explicit VR Big Endian
                  is_explicit = true;
                }
                else if (strncmp (reinterpret_cast<const char*> (data), "1.2.840.10008.1.2", size) == 0) {
                  is_BE = is_transfer_syntax_BE = false; // Implicit VR Little Endian
                  is_explicit = false;
                }
                else if (strncmp (reinterpret_cast<const char*> (data), "1.2.840.10008.1.2.1.99", size) == 0) {
                  throw Exception ("DICOM deflated explicit VR little endian transfer syntax not supported");
                }
                else WARN ("unknown DICOM transfer syntax: \"" + std::string (reinterpret_cast<const char*> (data), size) 
                    + "\" in file \"" + fmap->name() + "\" - ignored");
                break;
            }

            break;
        }

        return true;
      }












      Element::Type Element::type () const
      {
        if (!VR) return INVALID;
        if (VR == VR_FD || VR == VR_FL) return FLOAT;
        if (VR == VR_SL || VR == VR_SS) return INT;
        if (VR == VR_UL || VR == VR_US) return UINT;
        if (VR == VR_SQ) return SEQ;
        if (VR == VR_AE || VR == VR_AS || VR == VR_CS || VR == VR_DA ||
            VR == VR_DS || VR == VR_DT || VR == VR_IS || VR == VR_LO ||
            VR == VR_LT || VR == VR_PN || VR == VR_SH || VR == VR_ST ||
            VR == VR_TM || VR == VR_UI || VR == VR_UT || VR == VR_AT) return STRING;
        return OTHER;
      }



      std::vector<int32_t> Element::get_int () const
      {
        std::vector<int32_t> V;
        if (VR == VR_SL) 
          for (const uint8_t* p = data; p < data + size; p += sizeof (int32_t))
            V.push_back (get<int32_t> (p, is_BE));
        else if (VR == VR_SS)
          for (const uint8_t* p = data; p < data + size; p += sizeof (int16_t)) 
            V.push_back (get<int16_t> (p, is_BE));
        else if (VR == VR_IS) {
          std::vector<std::string> strings (split (std::string (reinterpret_cast<const char*> (data), size), "\\", false));
          V.resize (strings.size());
          for (size_t n = 0; n < V.size(); n++) 
            V[n] = to<int32_t> (strings[n]);
        }
        return V;
      }




      std::vector<uint32_t> Element::get_uint () const
      {
        std::vector<uint32_t> V;
        if (VR == VR_UL) 
          for (const uint8_t* p = data; p < data + size; p += sizeof (uint32_t))
            V.push_back (get<uint32_t> (p, is_BE));
        else if (VR == VR_US)
          for (const uint8_t* p = data; p < data + size; p += sizeof (uint16_t)) 
            V.push_back (get<uint16_t> (p, is_BE));
        else if (VR == VR_IS) {
          std::vector<std::string> strings (split (std::string (reinterpret_cast<const char*> (data), size), "\\", false));
          V.resize (strings.size());
          for (size_t n = 0; n < V.size(); n++) V[n] = to<uint32_t> (strings[n]);
        }
        return V;
      }



      std::vector<double> Element::get_float () const
      {
        std::vector<double> V;
        if (VR == VR_FD) 
          for (const uint8_t* p = data; p < data + size; p += sizeof (float64))
            V.push_back (get<float64> (p, is_BE));
        else if (VR == VR_FL)
          for (const uint8_t* p = data; p < data + size; p += sizeof (float32)) 
            V.push_back (get<float32> (p, is_BE));
        else if (VR == VR_DS) {
          std::vector<std::string> strings (split (std::string (reinterpret_cast<const char*> (data), size), "\\", false));
          V.resize (strings.size());
          for (size_t n = 0; n < V.size(); n++) 
            V[n] = to<double> (strings[n]);
        }
        return V;
      }



      

      std::vector<std::string> Element::get_string () const
      { 
        if (VR == VR_AT) {
          std::vector<std::string> strings;
          strings.push_back (printf ("%02X %02X", get<uint16_t> (data, is_BE), get<uint16_t> (data+2, is_BE)));
          return strings;
        }

        std::vector<std::string> strings (split (std::string (reinterpret_cast<const char*> (data), size), "\\", false)); 
        for (std::vector<std::string>::iterator i = strings.begin(); i != strings.end(); ++i) {
          *i = strip (*i);
          replace (*i, '^', ' ');
        }
        return strings;
      }



      namespace {
        template <class T> 
          inline void print_vec (const std::vector<T>& V)
          { 
            for (size_t n = 0; n < V.size(); n++) 
              fprintf (stdout, "%s ", str (V[n]).c_str()); 
          }
      }











      std::ostream& operator<< (std::ostream& stream, const Element& item)
      {
        const std::string& name (item.tag_name());

        stream << "[DCM] ";
        size_t indent = item.level() + ( item.VR == VR_SQ ? 0 : 1 );
        for (size_t i = 0; i < indent; i++) 
          stream << "  ";
        if (item.VR == VR_SQ) 
          stream << "+ ";
        else if (item.group == GROUP_SEQUENCE && item.element == ELEMENT_SEQUENCE_ITEM) 
          stream << "- ";
        else 
          stream << "  ";
        stream << printf ("%02X %02X ", item.group, item.element)  
            + reinterpret_cast<const char*> (&item.VR)[1] + reinterpret_cast<const char*> (&item.VR)[0] + " " 
            + str ( item.size == LENGTH_UNDEFINED ? 0 : item.size ) + " " 
            + str (item.offset (item.start)) + " " + ( name.size() ? name.substr (2) : "unknown" ) + " ";


        switch (item.type()) {
          case Element::INT: 
            stream << item.get_int(); 
            break;
          case Element::UINT:
            stream << item.get_uint(); 
            break;
          case Element::FLOAT:
            stream << item.get_float(); 
            break;
          case Element::STRING:
            if (item.group == GROUP_DATA && item.element == ELEMENT_DATA) 
              stream << "(data)";
            else 
              stream << item.get_string(); 
            break;
          case Element::SEQ:
            break;
          default:
            if (item.group != GROUP_SEQUENCE || item.element != ELEMENT_SEQUENCE_ITEM)
              stream << "unknown data type";
        }
        if (item.group%2) 
          stream << " [ PRIVATE ]";

        stream << "\n";

        return stream;
      }


    }
  }
}

