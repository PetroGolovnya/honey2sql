// honey2sql.cpp : main project file.

#include "stdafx.h"
#include "honey2sql.h"

using namespace System;
using namespace System::IO;
using namespace System::Collections::Generic;
using namespace System::Configuration;
using namespace System::Data;
using namespace System::Data::Sql;
using namespace System::Data::SqlClient;
using namespace System::Data::SqlTypes;
using namespace System::Globalization;
using namespace Gnu::Getopt;
using namespace std;

int main(array<System::String ^> ^args)
{
	Getopt^ g = gcnew Getopt("honey2sql", args, "s:n:d:h?");

	String^ server = "";
	int num_hists = -1;
	char num[5];
	DateTime start_date = DateTime(0001, 01, 01, 00, 00, 00);
	int c;
	String^ arg;

	while( (c = g->getopt()) != -1)
	{
		switch (c)
		{			
            case 's':
				server = g->Optarg;				
            	break;          				
            case 'd':
				arg = g->Optarg;
				start_date = DateTime::ParseExact(arg->ToString(), "dd.MM.yyyy hh:mm", CultureInfo::InvariantCulture);            			
            	break;
			case 'n':
				arg = g->Optarg;
				sprintf_s(num, "%s", arg);
				num_hists = atoi(num);
				break;            
            case '?':
			case 'h':
				dispaly_usage();
				exit(EXIT_SUCCESS);
            	break; // getopt() already printed an error            
            default:
				dispaly_usage();
				exit(EXIT_SUCCESS);
            	break;		
		}
	}

	if ( server == "" || num_hists == -1 ) 
	{
		dispaly_usage();
		exit(EXIT_FAILURE);
	}

	if ( start_date == DateTime(0001, 01, 01, 00, 00, 00) ) //≈сли дата не задана - читаем с даты вида dd.MM.yyyy 04:00
	{
		DateTime date = DateTime::Now;
		TimeSpan ts = date.TimeOfDay;
		start_date = (date - ts ).AddHours(4);
	}

	Console::WriteLine("S - {0} - D - {1} - N - {2}", server, start_date, num_hists);	

	array<String^>^ points = System::IO::File::ReadAllLines(L"c:\\honey2sql\\tags.txt");
	int size = points->Length;
	if (size <= 0)
	{
		Console::WriteLine("Ошибка при чтении файла. Файл пуст!");		
		exit(EXIT_FAILURE);
	}

	String^ param = ConfigurationManager::AppSettings["param_name"];		
	int status;

	POINT_NUMBER_DATA_2 p_points[1];
	PARAM_NUMBER_DATA_2 p_params[1];
	rgethstpar_date_data_2 gethstpar_date_data2[1];
	char pnt[15];
	char prm[10];
	char srv[7];
	sprintf_s(srv, "%s", server);

	char archive_path[] = "";
	float *hist_values = new float[num_hists];
		
	UInt32 hist_start_date = (UInt32)start_date.Subtract(DateTime(1981, 1, 1, 0, 0, 0)).Days;
  Single hist_start_time = (Single)(start_date.Hour * 60  * 60 + start_date.Minute * 60 + start_date.Second);	
	
	DataTable^ dt;
	dt = gcnew DataTable();
	dt->Columns->Add(L"date", DateTime::typeid);
	dt->Columns->Add(L"tag", String::typeid);
	dt->Columns->Add(L"value", Single::typeid);
	dt->Columns->Add(L"quality", Int16::typeid);	
	DataRow^ row;	

	//read point loop
	for each (String^ point in points)
	{	
		sprintf_s(pnt, "%s", point);		
		p_points[0].szPntName = pnt;
		
		status = point_numbers(srv, 1, p_points);
		
		if (status != 0)
		{			
			Console::WriteLine("Point_number resolution error 0x" + p_points[0].fStatus.ToString("X4") + " " + "at" + " " + point);						
			continue;
		}

		sprintf_s(prm, "%s", param);
		p_params[0].nPnt = p_points[0].nPnt;
		p_params[0].szPrmName = prm;

		status = param_numbers(srv, 1, p_params);
		
		if (status != 0)
		{
			Console::WriteLine("Param_number resolution error 0x" + p_params[0].fStatus.ToString("X4") + " " + "at" + " " + point + " " + "and" + " " + param);			
			continue;
		}

		gethstpar_date_data2[0].hist_type = HST_1HOUR;
		gethstpar_date_data2[0].hist_start_date = hist_start_date;
		gethstpar_date_data2[0].hist_start_time = hist_start_time;
		gethstpar_date_data2[0].num_hist = num_hists;
		gethstpar_date_data2[0].num_points = 1;
		gethstpar_date_data2[0].point_type_nums = &p_points[0].nPnt;
		gethstpar_date_data2[0].point_params = &p_params[0].nPrm;
		gethstpar_date_data2[0].archive_path = archive_path;
		gethstpar_date_data2[0].hist_values = hist_values;

		status = get_hist_values(srv, 1, gethstpar_date_data2);
		
		if (status != 0)
		{
			Console::WriteLine("Get history values error 0x" + status.ToString("X4") + " " + "at" + " " + point + " " + "and" + " " + param);		
			continue;
		}

		for (int i = 0; i < num_hists; i++)
		{
			status = hsc_bad_value(gethstpar_date_data2[0].hist_values[i]);
      Int16 quality = status ? 0 : 192;//if 1 - BAD

			row = dt->NewRow();
			row["date"] = start_date.AddHours(-i);
			row["tag"] = point;
      row["value"] = gethstpar_date_data2[0].hist_values[i];
			row["quality"] = quality;
			dt->Rows->Add(row);
/*			printf("VALUE - %d - %d - %.2f - %.2f - %d\n",
				gethstpar_date_data2[0].point_type_nums[0],
				gethstpar_date_data2[0].hist_start_date,
				gethstpar_date_data2[0].hist_start_time,
				gethstpar_date_data2[0].hist_values[i],
				quality
				);  */          
		}		
	}

	Array::Clear(points, 0, size);	

	//insert datatable to SQL
	ConnectionStringSettings^ sqlconstr = ConfigurationManager::ConnectionStrings["archmConnectionString"];
	SqlConnection^ sqlcon = gcnew SqlConnection();
	sqlcon->ConnectionString = sqlconstr->ConnectionString;
	try
	{
		sqlcon->Open();
	}
	catch (Exception^ ex)
	{
		Console::WriteLine("Can`t connect to SQL server {0}", ex->ToString());		
	}
	String^ create_temp = L"CREATE table #temp_7 (date [datetime] NOT NULL, tag [char](15) COLLATE database_default NOT NULL, value [float] NULL, quality [smallint] NOT NULL)"; //COLLATE database_default - tempdb и base могут иметь разный тип сортировки. 
	SqlCommand^ myCmd = gcnew SqlCommand(create_temp, sqlcon);
	try
	{
		myCmd->ExecuteNonQuery();
	}
	catch (Exception^ ex)
	{
		Console::WriteLine("Temporary table creation error {0}", ex->ToString());
	}

	SqlDataAdapter^ adapter = gcnew SqlDataAdapter();
	adapter->InsertCommand = gcnew SqlCommand("INSERT INTO #temp_7 ([date], [tag], [value], [quality]) VALUES (@date, @tag, @value, @quality)", sqlcon);
	adapter->InsertCommand->Parameters->AddWithValue("@date", DbType::DateTime)->SourceColumn = "date";
	adapter->InsertCommand->Parameters->AddWithValue("@tag", DbType::String)->SourceColumn = "tag";
	adapter->InsertCommand->Parameters->AddWithValue("@value", DbType::Single)->SourceColumn = "value";
	adapter->InsertCommand->Parameters->AddWithValue("@quality", DbType::Int16)->SourceColumn = "quality";
	try
	{
		adapter->Update(dt);
	}
	catch (Exception^ ex)
	{
		Console::WriteLine("Dataset insert error" + " " + ex->ToString());		
	}

	String^ merge = L"MERGE INTO [dbo].[block_7] AS T " +
		        "USING (SELECT * FROM #temp_7) AS S " +
					  "ON (T.date = S.date AND T.tag = S.tag) " +
					  "WHEN MATCHED THEN " +
					  "UPDATE SET " +
					  "T.value = S.value, T.quality = S.quality " +
					  "WHEN NOT MATCHED THEN " +
					  "INSERT (date, tag, value, quality) " +
					  "VALUES (S.date, S.tag, S.value, S.quality " +
					  ");";
	myCmd = gcnew SqlCommand(merge, sqlcon);
	try
	{
		myCmd->ExecuteNonQuery();
	}
	catch (Exception^ ex)
	{
		Console::WriteLine("MERGE error" + " " + ex->ToString());		
	}

	String^ drop = L"DROP TABLE #temp_7";
	myCmd = gcnew SqlCommand(drop, sqlcon);
	try
	{
		myCmd->ExecuteNonQuery();
	}
	catch (Exception^ ex)
	{
		Console::WriteLine("Drop table error" + " " + ex->ToString());		
	}

	sqlcon->Close();
	
	delete [] hist_values;

    return 0;
}

int point_numbers(char *hostname, int num_points, POINT_NUMBER_DATA_2 *get_pnt)
{	
	int status = rhsc_point_numbers_2(hostname, num_points, get_pnt);	
	return status;
}

int param_numbers(char *hostname, int num_params, PARAM_NUMBER_DATA_2 *get_par)
{
	int status = rhsc_param_numbers_2(hostname, num_params, get_par);
	return status;
}

int get_hist_values(char *hostname, int num_hist_data, rgethstpar_date_data_str_2 *hist_data)
{
	int status = rhsc_param_hist_dates_2(hostname, num_hist_data, hist_data);		
	return status;
}

void dispaly_usage()
{	
	Console::WriteLine(L"»спользование: honey2sql -s SRV7 -n 25 [-d \"06.06.2017 04:00\"]\n" +
	  "-h помощь\n" +
      "-s SRV7  сервер\n" +
      "-n 25 число записей\n" +
      "-d \"06.06.2017 04:00\" конечна¤ дата");
	return;
}

