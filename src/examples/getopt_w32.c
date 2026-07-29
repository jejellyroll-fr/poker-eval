#include <stdio.h>
#include <string.h>

int optind = 1;
char *optarg = NULL;
int opterr = 1;

int getopt(int argc, char *argv[], const char *optstring) {
    static int letter_index = 0;
    char scan_char;

    optarg = NULL;

    // Vérifier si nous avons dépassé le nombre d'arguments
    if (optind >= argc) {
        return -1;
    }

    // Récupérer l'argument actuel
    char *arg = argv[optind];

    // Vérifier si l'argument est une option (commence par '-' ou '/')
    if (arg[0] != '-' && arg[0] != '/') {
        return -1;
    }

    // Vérifier si c'est la fin des options "--"
    if (arg[1] == '-' || arg[1] == '/') {
        optind++;
        return -1;
    }

    // Passer le caractère '-' ou '/'
    scan_char = arg[++letter_index];

    // Vérifier si fin du groupe d'options
    if (scan_char == '\0') {
        optind++;
        letter_index = 0;
        return getopt(argc, argv, optstring); // Récursion pour passer au prochain argument
    }

    // Rechercher le caractère d'option dans optstring
    char *opt_loc = strchr(optstring, scan_char);
    if (opt_loc == NULL || scan_char == ':') {
        // Option inconnue
        if (opterr) {
            fprintf(stderr, "Option inconnue: -%c\n", scan_char);
        }
        letter_index++;
        return '?';
    }

    // Vérifier si l'option nécessite un argument
    if (*(opt_loc + 1) == ':') {
        // Réinitialiser l'index pour le prochain appel
        letter_index = 0;

        // Passer à l'argument suivant pour récupérer l'argument de l'option
        if (++optind < argc) {
            optarg = argv[optind];
        } else {
            // Option avec argument manquant
            if (opterr) {
                fprintf(stderr, "Argument manquant pour l'option: -%c\n", scan_char);
            }
            return ':';
        }
    }

    // Préparer pour le prochain appel à getopt
    if (argv[optind][letter_index] == '\0') {
        optind++;
        letter_index = 0;
    }

    return scan_char;
}
